// server/HttpServer.h
#pragma once

#include "../api/BackendAPI.h"
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <sstream>
#include <regex>
#include "../json.hpp"

using namespace std;

class HttpServer
{
public:
    struct HttpRequest
    {
        string method;
        string path;
        string version;
        unordered_map<string, string> headers;
        unordered_map<string, string> query_params;
        string body;
        string client_ip;
        int client_port;
    };

    struct HttpResponse
    {
        int status_code;
        string status_text;
        unordered_map<string, string> headers;
        string body;

        HttpResponse() : status_code(200), status_text("OK")
        {
            headers["Content-Type"] = "text/html";
            headers["Server"] = "TradingPlatform/1.0";
        }
    };

    using RouteHandler = function<HttpResponse(const HttpRequest &)>;

    HttpServer(int port, BackendAPI *api, int thread_pool_size = 4);
    ~HttpServer();

    void start();
    void stop();
    bool isRunning() const { return running_; }
    int getPort() const { return port_; }

    // Route registration
    void addRoute(const string &method, const string &path, RouteHandler handler);
    void addStaticRoute(const string &url_path, const string &file_path);

private:
    int port_;
    BackendAPI *api_;
    atomic<bool> running_;
    int server_fd_;

    // Thread pool
    vector<thread> worker_threads_;
    queue<pair<int, string>> connection_queue_;
    mutex queue_mutex_;
    condition_variable queue_cv_;
    int thread_pool_size_;

    // Routes
    unordered_map<string, RouteHandler> routes_;
    unordered_map<string, string> static_routes_;
    mutex routes_mutex_;

    // Socket utilities
    bool setupSocket();
    void acceptConnections();
    void workerThread();
    void handleConnection(int client_fd, const string &client_ip);

    // HTTP processing
    HttpRequest parseRequest(const string &raw_request, const string &client_ip);
    string buildResponse(const HttpResponse &response);
    HttpResponse processRequest(const HttpRequest &request);

    // Route matching
    RouteHandler findRouteHandler(const string &method, const string &path);
    bool matchRoute(const string &route_pattern, const string &path,
                    unordered_map<string, string> &params);

    // Default handlers
    HttpResponse handleNotFound(const HttpRequest &request);
    HttpResponse handleInternalError(const HttpRequest &request, const string &error);
    HttpResponse handleStaticFile(const HttpRequest &request, const string &file_path);

    // API handlers
    HttpResponse handleApiRegister(const HttpRequest &request);
    HttpResponse handleApiLogin(const HttpRequest &request);
    HttpResponse handleApiPlaceOrder(const HttpRequest &request);
    HttpResponse handleApiCancelOrder(const HttpRequest &request);
    HttpResponse handleApiGetOrderBook(const HttpRequest &request);
    HttpResponse handleApiGetPortfolio(const HttpRequest &request);
    HttpResponse handleApiGetLeaderboard(const HttpRequest &request);
    HttpResponse handleApiGetSymbols(const HttpRequest &request);
    HttpResponse handleApiSearchSymbols(const HttpRequest &request);
    HttpResponse handleApiGetTrades(const HttpRequest &request);
    HttpResponse handleApiGetBalance(const HttpRequest &request);
    HttpResponse handleApiGetStatus(const HttpRequest &request);

    // Utility functions
    string urlDecode(const string &str);
    string getMimeType(const string &extension);
    string readFile(const string &path);
    string getClientIp(int client_fd);
    void logRequest(const HttpRequest &request, const HttpResponse &response);
    void initializeRoutes();

    // CORS support
    void addCorsHeaders(HttpResponse &response);
};