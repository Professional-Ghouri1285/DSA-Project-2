// server/HttpServer.cpp
#include "HttpServer.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iomanip>
#include <ctime>

// Socket includes for Windows compatibility
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

HttpServer::HttpServer(int port, BackendAPI *api, int thread_pool_size)
    : port_(port), api_(api), running_(false), server_fd_(-1),
      thread_pool_size_(thread_pool_size)
{

    initializeRoutes();
    cout << "[HTTP Server] Initialized on port " << port << "\n";
}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::start()
{
    if (running_)
        return;

    if (!setupSocket())
    {
        cerr << "[HTTP Server] Failed to setup socket\n";
        return;
    }

    running_ = true;

    // Start worker threads
    for (int i = 0; i < thread_pool_size_; ++i)
    {
        worker_threads_.emplace_back(&HttpServer::workerThread, this);
    }

    // Start accepting connections
    thread acceptor(&HttpServer::acceptConnections, this);
    acceptor.detach();

    cout << "[HTTP Server] Started on port " << port_ << "\n";
    cout << "[HTTP Server] Thread pool size: " << thread_pool_size_ << "\n";
}

void HttpServer::stop()
{
    if (!running_)
        return;

    running_ = false;

    // Wake up all waiting threads
    queue_cv_.notify_all();

    // Close server socket
    if (server_fd_ >= 0)
    {
        close(server_fd_);
    }

    // Join worker threads
    for (auto &thread : worker_threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    worker_threads_.clear();

    cout << "[HTTP Server] Stopped\n";
}

bool HttpServer::setupSocket()
{
    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
    {
        cerr << "[HTTP Server] Socket creation failed\n";
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        cerr << "[HTTP Server] Failed to set SO_REUSEADDR\n";
        close(server_fd_);
        return false;
    }

#ifdef SO_REUSEPORT
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        cerr << "[HTTP Server] Failed to set SO_REUSEPORT\n";
    }
#endif

    // Set non-blocking mode
    int flags = fcntl(server_fd_, F_GETFL, 0);
    fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

    // Bind socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        cerr << "[HTTP Server] Bind failed on port " << port_ << "\n";
        close(server_fd_);
        return false;
    }

    // Listen
    if (listen(server_fd_, SOMAXCONN) < 0)
    {
        cerr << "[HTTP Server] Listen failed\n";
        close(server_fd_);
        return false;
    }

    return true;
}

void HttpServer::acceptConnections()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (running_)
    {
        int client_fd = accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0)
        {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
            {
                cerr << "[HTTP Server] Accept error: " << strerror(errno) << "\n";
            }
            this_thread::sleep_for(chrono::milliseconds(10));
            continue;
        }

        // Get client IP
        string client_ip = inet_ntoa(client_addr.sin_addr);
        int client_port = ntohs(client_addr.sin_port);

        // Add to connection queue
        {
            lock_guard<mutex> lock(queue_mutex_);
            connection_queue_.push({client_fd, client_ip});
        }
        queue_cv_.notify_one();

        cout << "[HTTP Server] New connection from " << client_ip << ":" << client_port << "\n";
    }
}

void HttpServer::workerThread()
{
    while (running_)
    {
        pair<int, string> connection;

        {
            unique_lock<mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]()
                           { return !connection_queue_.empty() || !running_; });

            if (!running_ && connection_queue_.empty())
            {
                return;
            }

            if (!connection_queue_.empty())
            {
                connection = connection_queue_.front();
                connection_queue_.pop();
            }
            else
            {
                continue;
            }
        }

        handleConnection(connection.first, connection.second);
    }
}

void HttpServer::handleConnection(int client_fd, const string &client_ip)
{
    char buffer[8192] = {0};
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0)
    {
        close(client_fd);
        return;
    }

    string raw_request(buffer, bytes_read);

    try
    {
        HttpRequest request = parseRequest(raw_request, client_ip);
        HttpResponse response = processRequest(request);

        // Log the request
        logRequest(request, response);

        string response_str = buildResponse(response);
        send(client_fd, response_str.c_str(), response_str.length(), 0);
    }
    catch (const exception &e)
    {
        cerr << "[HTTP Server] Error handling request: " << e.what() << "\n";

        HttpResponse error_response;
        error_response.status_code = 500;
        error_response.status_text = "Internal Server Error";
        error_response.body = "<h1>500 Internal Server Error</h1>";
        error_response.headers["Content-Type"] = "text/html";

        string error_str = buildResponse(error_response);
        send(client_fd, error_str.c_str(), error_str.length(), 0);
    }

    close(client_fd);
}

HttpServer::HttpRequest HttpServer::parseRequest(const string &raw_request, const string &client_ip)
{
    HttpRequest request;
    request.client_ip = client_ip;

    istringstream request_stream(raw_request);
    string line;

    // Parse request line
    if (!getline(request_stream, line))
    {
        throw runtime_error("Empty request");
    }

    istringstream request_line(line);
    request_line >> request.method >> request.path >> request.version;

    // Remove \r if present
    if (!request.version.empty() && request.version.back() == '\r')
    {
        request.version.pop_back();
    }

    // Parse headers
    while (getline(request_stream, line) && line != "\r" && !line.empty())
    {
        size_t colon_pos = line.find(':');
        if (colon_pos != string::npos)
        {
            string key = line.substr(0, colon_pos);
            string value = line.substr(colon_pos + 2); // Skip ": "

            // Remove \r if present
            if (!value.empty() && value.back() == '\r')
            {
                value.pop_back();
            }

            request.headers[key] = value;
        }
    }

    // Parse query parameters
    size_t query_pos = request.path.find('?');
    if (query_pos != string::npos)
    {
        string query_string = request.path.substr(query_pos + 1);
        request.path = request.path.substr(0, query_pos);

        istringstream query_stream(query_string);
        string pair;
        while (getline(query_stream, pair, '&'))
        {
            size_t equals_pos = pair.find('=');
            if (equals_pos != string::npos)
            {
                string key = urlDecode(pair.substr(0, equals_pos));
                string value = urlDecode(pair.substr(equals_pos + 1));
                request.query_params[key] = value;
            }
            else if (!pair.empty())
            {
                request.query_params[urlDecode(pair)] = "";
            }
        }
    }

    // Parse body
    if (request.headers.count("Content-Length"))
    {
        try
        {
            size_t content_length = stoul(request.headers.at("Content-Length"));
            if (content_length > 0)
            {
                request.body.resize(content_length);
                request_stream.read(&request.body[0], content_length);
            }
        }
        catch (...)
        {
            throw runtime_error("Invalid Content-Length");
        }
    }
    else if (request.headers.count("Transfer-Encoding") &&
             request.headers.at("Transfer-Encoding") == "chunked")
    {
        // Handle chunked encoding (simplified)
        while (getline(request_stream, line))
        {
            if (line == "0\r" || line == "0")
                break;
            size_t chunk_size;
            stringstream(line) >> hex >> chunk_size;
            if (chunk_size == 0)
                break;

            vector<char> chunk(chunk_size);
            request_stream.read(chunk.data(), chunk_size);
            request.body.append(chunk.data(), chunk.size());

            // Skip \r\n after chunk
            request_stream.ignore(2);
        }
    }

    return request;
}

string HttpServer::buildResponse(const HttpResponse &response)
{
    ostringstream response_stream;

    // Status line
    response_stream << "HTTP/1.1 " << response.status_code << " " << response.status_text << "\r\n";

    // Headers
    for (const auto &header : response.headers)
    {
        response_stream << header.first << ": " << header.second << "\r\n";
    }

    // Content-Length
    response_stream << "Content-Length: " << response.body.size() << "\r\n";

    // End of headers
    response_stream << "\r\n";

    // Body
    response_stream << response.body;

    return response_stream.str();
}

HttpServer::HttpResponse HttpServer::processRequest(const HttpRequest &request)
{
    cout << "\n[DEBUG] === Processing Request ===" << endl;
    cout << "[DEBUG] Method: " << request.method << endl;
    cout << "[DEBUG] Path: " << request.path << endl;
    cout << "[DEBUG] Client IP: " << request.client_ip << endl;

    try
    {
        // FIRST: Check for API routes (exact matches should take priority)
        cout << "[DEBUG] Checking API routes first..." << endl;
        RouteHandler handler = findRouteHandler(request.method, request.path);
        if (handler)
        {
            cout << "[DEBUG] Found API handler for: " << request.path << endl;
            return handler(request);
        }

        // SECOND: Check for static file routes
        cout << "[DEBUG] No API route found, checking static routes..." << endl;
        for (const auto &route : static_routes_)
        {
            cout << "[DEBUG] Checking static route: " << route.first << " -> " << route.second << endl;

            // For the root route "/", we need exact prefix matching
            if (route.first == "/")
            {
                // Root route should only match if path is "/" or starts with known static extensions
                if (request.path == "/" ||
                    request.path == "/index.html" ||
                    request.path.find("/css/") == 0 ||
                    request.path.find("/js/") == 0 ||
                    request.path.find("/images/") == 0)
                {
                    string file_path = route.second;
                    if (request.path != "/")
                    {
                        file_path += request.path.substr(1); // Remove leading slash
                    }
                    else
                    {
                        file_path += "index.html"; // Default to index.html for root
                    }

                    cout << "[DEBUG] Matched root route, file_path: " << file_path << endl;
                    return handleStaticFile(request, file_path);
                }
            }
            // For other static routes (css/, js/, images/)
            else if (request.path.find(route.first) == 0)
            {
                string file_path = route.second + request.path.substr(route.first.length());
                cout << "[DEBUG] Matched static route, file_path: " << file_path << endl;
                return handleStaticFile(request, file_path);
            }
        }

        // THIRD: Try to serve as static file from frontend directory (for backward compatibility)
        if (request.method == "GET")
        {
            cout << "[DEBUG] Trying as static file: frontend" << request.path << endl;
            return handleStaticFile(request, "frontend" + request.path);
        }

        cout << "[DEBUG] No route found, returning 404" << endl;
        return handleNotFound(request);
    }
    catch (const exception &e)
    {
        cerr << "[ERROR] Exception in processRequest: " << e.what() << endl;
        return handleInternalError(request, e.what());
    }
}

void HttpServer::addRoute(const string &method, const string &path, RouteHandler handler)
{
    lock_guard<mutex> lock(routes_mutex_);
    string key = method + " " + path;
    routes_[key] = handler;
}

void HttpServer::addStaticRoute(const string &url_path, const string &file_path)
{
    static_routes_[url_path] = file_path;
}

HttpServer::RouteHandler HttpServer::findRouteHandler(const string &method, const string &path)
{
    lock_guard<mutex> lock(routes_mutex_);

    // Exact match
    string key = method + " " + path;
    auto it = routes_.find(key);
    if (it != routes_.end())
    {
        return it->second;
    }

    // Try with trailing slash
    if (!path.empty() && path.back() != '/')
    {
        key = method + " " + path + "/";
        it = routes_.find(key);
        if (it != routes_.end())
        {
            return it->second;
        }
    }

    // Remove trailing slash
    if (!path.empty() && path.back() == '/')
    {
        key = method + " " + path.substr(0, path.length() - 1);
        it = routes_.find(key);
        if (it != routes_.end())
        {
            return it->second;
        }
    }

    return nullptr;
}

HttpServer::HttpResponse HttpServer::handleNotFound(const HttpRequest &request)
{
    HttpResponse response;
    response.status_code = 404;
    response.status_text = "Not Found";
    response.headers["Content-Type"] = "text/html";

    stringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head><title>404 Not Found</title></head>\n"
         << "<body>\n"
         << "<h1>404 Not Found</h1>\n"
         << "<p>The requested URL " << request.path << " was not found on this server.</p>\n"
         << "<hr>\n"
         << "<address>TradingPlatform/1.0</address>\n"
         << "</body>\n"
         << "</html>\n";

    response.body = html.str();
    return response;
}

HttpServer::HttpResponse HttpServer::handleInternalError(const HttpRequest &request, const string &error)
{
    HttpResponse response;
    response.status_code = 500;
    response.status_text = "Internal Server Error";
    response.headers["Content-Type"] = "text/html";

    stringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head><title>500 Internal Server Error</title></head>\n"
         << "<body>\n"
         << "<h1>500 Internal Server Error</h1>\n"
         << "<p>An internal server error occurred.</p>\n"
         << "<pre>" << error << "</pre>\n"
         << "<hr>\n"
         << "<address>TradingPlatform/1.0</address>\n"
         << "</body>\n"
         << "</html>\n";

    response.body = html.str();
    return response;
}

HttpServer::HttpResponse HttpServer::handleStaticFile(const HttpRequest &request, const string &file_path)
{
    HttpResponse response;

    cout << "[DEBUG] handleStaticFile called with path: " << file_path << endl;

    // Security check: prevent directory traversal
    if (file_path.find("..") != string::npos)
    {
        cerr << "[SECURITY] Directory traversal attempt: " << file_path << endl;
        response.status_code = 403;
        response.status_text = "Forbidden";
        response.body = "<h1>403 Forbidden</h1>";
        response.headers["Content-Type"] = "text/html";
        return response;
    }

    // Check if path ends with slash (directory)
    string actual_path = file_path;

    // If path ends with /, it's a directory, serve index.html
    if (!actual_path.empty() && actual_path.back() == '/')
    {
        actual_path += "index.html";
    }

    // If path doesn't have extension, try adding .html
    if (actual_path.find('.') == string::npos)
    {
        // Check if file exists without extension
        struct stat buffer;
        if (stat(actual_path.c_str(), &buffer) != 0)
        {
            // Try with .html extension
            string with_html = actual_path + ".html";
            if (stat(with_html.c_str(), &buffer) == 0)
            {
                actual_path = with_html;
            }
        }
    }

    cout << "[DEBUG] Resolved path to: " << actual_path << endl;

    // Check if file exists
    struct stat file_stat;
    if (stat(actual_path.c_str(), &file_stat) != 0)
    {
        cerr << "[ERROR] File not found: " << actual_path
             << " (errno: " << errno << ")" << endl;
        return handleNotFound(request);
    }

    // Check if it's a regular file
    if (!S_ISREG(file_stat.st_mode))
    {
        cerr << "[ERROR] Not a regular file: " << actual_path << endl;
        response.status_code = 403;
        response.status_text = "Forbidden";
        response.body = "<h1>403 Forbidden - Not a regular file</h1>";
        response.headers["Content-Type"] = "text/html";
        return response;
    }

    // Open file
    ifstream file(actual_path, ios::binary);
    if (!file.is_open())
    {
        cerr << "[ERROR] Cannot open file: " << actual_path << endl;
        return handleNotFound(request);
    }

    try
    {
        // Get file size
        file.seekg(0, ios::end);
        streamsize file_size = file.tellg();
        file.seekg(0, ios::beg);

        cout << "[DEBUG] File size: " << file_size << " bytes" << endl;

        // Check for empty file
        if (file_size <= 0)
        {
            cout << "[DEBUG] Empty file or size unknown" << endl;

            // Read file character by character
            string content;
            char ch;
            while (file.get(ch) && content.size() < 10 * 1024 * 1024) // 10MB limit
            {
                content.push_back(ch);
            }

            file_size = content.size();

            // Determine MIME type
            size_t dot_pos = actual_path.find_last_of('.');
            string extension = (dot_pos != string::npos) ? actual_path.substr(dot_pos + 1) : "";
            string mime_type = getMimeType(extension);

            response.status_code = 200;
            response.status_text = "OK";
            response.headers["Content-Type"] = mime_type;
            response.headers["Cache-Control"] = "public, max-age=3600";
            response.body = content;

            cout << "[DEBUG] Served empty/small file: " << actual_path
                 << " (" << file_size << " bytes)" << endl;
        }
        else
        {
            // Read entire file
            vector<char> buffer(file_size);
            if (!file.read(buffer.data(), file_size))
            {
                throw runtime_error("Failed to read entire file");
            }

            // Determine MIME type
            size_t dot_pos = actual_path.find_last_of('.');
            string extension = (dot_pos != string::npos) ? actual_path.substr(dot_pos + 1) : "";
            string mime_type = getMimeType(extension);

            response.status_code = 200;
            response.status_text = "OK";
            response.headers["Content-Type"] = mime_type;
            response.headers["Cache-Control"] = "public, max-age=3600";
            response.body = string(buffer.data(), buffer.size());

            cout << "[DEBUG] Served file: " << actual_path
                 << " (" << file_size << " bytes)" << endl;
        }
    }
    catch (const bad_alloc &e)
    {
        cerr << "[ERROR] Memory allocation failed for file: " << actual_path
             << " - " << e.what() << endl;

        // Try to serve a minimal error page
        response.status_code = 500;
        response.status_text = "Internal Server Error";
        response.body = "<h1>500 Internal Server Error</h1><p>File too large or memory error</p>";
        response.headers["Content-Type"] = "text/html";
        return response;
    }
    catch (const exception &e)
    {
        cerr << "[ERROR] Exception reading file " << actual_path
             << ": " << e.what() << endl;
        return handleInternalError(request, "Error reading file");
    }

    return response;
}

// API Handlers
HttpServer::HttpResponse HttpServer::handleApiRegister(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        json request_json = json::parse(request.body);
        json api_response = api_->handleRegister(request_json);

        response.status_code = api_response["success"] ? 200 : 400;
        response.status_text = api_response["success"] ? "OK" : "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiLogin(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        json request_json = json::parse(request.body);
        json api_response = api_->handleLogin(request_json);

        response.status_code = api_response["success"] ? 200 : 401;
        response.status_text = api_response["success"] ? "OK" : "Unauthorized";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiPlaceOrder(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        json request_json = json::parse(request.body);
        json api_response = api_->handlePlaceOrder(request_json);

        response.status_code = api_response["success"] ? 200 : 400;
        response.status_text = api_response["success"] ? "OK" : "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiCancelOrder(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        json request_json = json::parse(request.body);
        json api_response = api_->handleCancelOrder(request_json);

        response.status_code = api_response["success"] ? 200 : 400;
        response.status_text = api_response["success"] ? "OK" : "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiGetOrderBook(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        string symbol = request.query_params.count("symbol") ? request.query_params.at("symbol") : "AAPL";
        int depth = request.query_params.count("depth") ? stoi(request.query_params.at("depth")) : 10;

        json api_response = api_->handleGetOrderBook(symbol, depth);

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiGetPortfolio(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        if (!request.query_params.count("user_id"))
        {
            throw runtime_error("Missing user_id parameter");
        }

        int user_id = stoi(request.query_params.at("user_id"));
        json api_response = api_->handleGetPortfolio(user_id);

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiGetLeaderboard(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        int limit = request.query_params.count("limit") ? stoi(request.query_params.at("limit")) : 10;

        json api_response = api_->handleGetLeaderboard(limit);

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiGetSymbols(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        json api_response = api_->handleGetSymbols();

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiSearchSymbols(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        if (!request.query_params.count("q"))
        {
            throw runtime_error("Missing query parameter 'q'");
        }

        string query = request.query_params.at("q");
        json api_response = api_->handleSearchSymbols(query);

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiGetTrades(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        string symbol = request.query_params.count("symbol") ? request.query_params.at("symbol") : "AAPL";
        int limit = request.query_params.count("limit") ? stoi(request.query_params.at("limit")) : 50;

        json api_response = api_->handleGetTrades(symbol, limit);

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiGetBalance(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        if (!request.query_params.count("user_id"))
        {
            throw runtime_error("Missing user_id parameter");
        }

        int user_id = stoi(request.query_params.at("user_id"));
        json api_response = api_->handleGetBalance(user_id);

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

HttpServer::HttpResponse HttpServer::handleApiGetStatus(const HttpRequest &request)
{
    HttpResponse response;

    try
    {
        json api_response = api_->handleGetSystemStatus();

        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = api_response.dump();
    }
    catch (const exception &e)
    {
        response.status_code = 400;
        response.status_text = "Bad Request";
        response.headers["Content-Type"] = "application/json";
        addCorsHeaders(response);
        response.body = json{{"success", false}, {"error", e.what()}}.dump();
    }

    return response;
}

void HttpServer::initializeRoutes()
{
    // API routes should be added FIRST
    addRoute("GET", "/api/test", [this](const HttpRequest &req)
             {
        HttpResponse resp;
        resp.status_code = 200;
        resp.status_text = "OK";
        resp.headers["Content-Type"] = "application/json";
        addCorsHeaders(resp);
        resp.body = "{\"status\": \"Server is working\", \"timestamp\": " + 
                     to_string(time(nullptr)) + "}";
        return resp; });

    // ... rest of your existing API routes ...
    addRoute("POST", "/api/register",
             bind(&HttpServer::handleApiRegister, this, placeholders::_1));
    addRoute("POST", "/api/login",
             bind(&HttpServer::handleApiLogin, this, placeholders::_1));
    addRoute("POST", "/api/order",
             bind(&HttpServer::handleApiPlaceOrder, this, placeholders::_1));
    addRoute("DELETE", "/api/order",
             bind(&HttpServer::handleApiCancelOrder, this, placeholders::_1));
    addRoute("GET", "/api/orderbook",
             bind(&HttpServer::handleApiGetOrderBook, this, placeholders::_1));
    addRoute("GET", "/api/portfolio",
             bind(&HttpServer::handleApiGetPortfolio, this, placeholders::_1));
    addRoute("GET", "/api/leaderboard",
             bind(&HttpServer::handleApiGetLeaderboard, this, placeholders::_1));
    addRoute("GET", "/api/symbols",
             bind(&HttpServer::handleApiGetSymbols, this, placeholders::_1));
    addRoute("GET", "/api/search",
             bind(&HttpServer::handleApiSearchSymbols, this, placeholders::_1));
    addRoute("GET", "/api/trades",
             bind(&HttpServer::handleApiGetTrades, this, placeholders::_1));
    addRoute("GET", "/api/balance",
             bind(&HttpServer::handleApiGetBalance, this, placeholders::_1));
    addRoute("GET", "/api/status",
             bind(&HttpServer::handleApiGetStatus, this, placeholders::_1));

    // Static routes - be more specific
    // Don't use "/" as a static route - handle it specially
    static_routes_["/css/"] = "frontend/css/";
    static_routes_["/js/"] = "frontend/js/";
    static_routes_["/images/"] = "frontend/images/";

    // Add specific routes for common files
    addRoute("GET", "/", [this](const HttpRequest &req)
             { return handleStaticFile(req, "frontend/index.html"); });

    addRoute("GET", "/index.html", [this](const HttpRequest &req)
             { return handleStaticFile(req, "frontend/index.html"); });
}

// Utility functions
string HttpServer::urlDecode(const string &str)
{
    string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i)
    {
        if (str[i] == '%' && i + 2 < str.size())
        {
            int value;
            istringstream hex_stream(str.substr(i + 1, 2));
            if (hex_stream >> hex >> value)
            {
                result += static_cast<char>(value);
                i += 2;
            }
            else
            {
                result += str[i];
            }
        }
        else if (str[i] == '+')
        {
            result += ' ';
        }
        else
        {
            result += str[i];
        }
    }

    return result;
}

string HttpServer::getMimeType(const string &extension)
{
    static const unordered_map<string, string> mime_types = {
        {"html", "text/html"},
        {"htm", "text/html"},
        {"css", "text/css"},
        {"js", "application/javascript"},
        {"json", "application/json"},
        {"xml", "application/xml"},
        {"txt", "text/plain"},
        {"csv", "text/csv"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"ico", "image/x-icon"},
        {"pdf", "application/pdf"},
        {"zip", "application/zip"},
        {"tar", "application/x-tar"},
        {"gz", "application/gzip"},
        {"mp3", "audio/mpeg"},
        {"mp4", "video/mp4"},
        {"woff", "font/woff"},
        {"woff2", "font/woff2"},
        {"ttf", "font/ttf"},
        {"otf", "font/otf"}};

    string ext_lower = extension;
    transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), ::tolower);

    auto it = mime_types.find(ext_lower);
    if (it != mime_types.end())
    {
        return it->second;
    }

    return "application/octet-stream";
}

string HttpServer::readFile(const string &path)
{
    ifstream file(path, ios::binary);
    if (!file.is_open())
    {
        throw runtime_error("Cannot open file: " + path);
    }

    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

void HttpServer::logRequest(const HttpRequest &request, const HttpResponse &response)
{
    auto now = chrono::system_clock::now();
    auto now_time = chrono::system_clock::to_time_t(now);

    cout << "[" << put_time(localtime(&now_time), "%Y-%m-%d %H:%M:%S") << "] "
         << request.client_ip << " "
         << "\"" << request.method << " " << request.path << " " << request.version << "\" "
         << response.status_code << " "
         << response.body.size() << " bytes"
         << endl;
}

void HttpServer::addCorsHeaders(HttpResponse &response)
{
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
    response.headers["Access-Control-Max-Age"] = "86400";
}