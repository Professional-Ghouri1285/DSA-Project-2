// core/DatabaseInitializer.h
#pragma once
#include "../storage/FileManager.h"
#include "../storage/BufferManager.h"
#include "../storage/WAL.h"
#include "UserManager.h"
#include "MatchingEngine.h"
#include <iostream>

class DatabaseInitializer
{
public:
    static bool initializeDatabase(const std::string &db_file,
                                   FileManager *&fm,
                                   BufferManager *&bm,
                                   WriteAheadLog *&wal,
                                   UserManager *&user_mgr,
                                   MatchingEngine *&engine)
    {
        try
        {
            // Create FileManager
            fm = new FileManager(db_file);

            // Create BufferManager
            bm = new BufferManager(fm);

            // Create WAL
            wal = new WriteAheadLog(db_file + ".log");

            // Try to load existing roots
            int user_root = fm->getUserTreeRoot();
            int portfolio_root = fm->getPortfolioTreeRoot();

            std::cout << "Database initialization:" << std::endl;
            std::cout << "  User tree root: " << user_root << std::endl;
            std::cout << "  Portfolio tree root: " << portfolio_root << std::endl;

            // Create UserManager with loaded roots (or -1 if not found)
            user_mgr = new UserManager(bm, wal, user_root, portfolio_root);

            // Create MatchingEngine
            engine = new MatchingEngine(bm, wal, user_mgr);

            std::cout << "Database initialized successfully" << std::endl;
            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to initialize database: " << e.what() << std::endl;
            return false;
        }
    }

    static void shutdownDatabase(FileManager *fm,
                                 BufferManager *bm,
                                 WriteAheadLog *wal,
                                 UserManager *user_mgr,
                                 MatchingEngine *engine)
    {
        std::cout << "Shutting down database..." << std::endl;

        // Save all root pages
        if (user_mgr)
        {
            user_mgr->saveRootPages();
        }

        // Flush everything
        if (bm)
        {
            bm->flushAll();
        }

        // Delete in reverse order
        delete engine;
        delete user_mgr;
        delete wal;
        delete bm;
        delete fm;

        std::cout << "Database shutdown complete" << std::endl;
    }
};