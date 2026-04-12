// User.h
#pragma once
#include <cstdint>
#include <string>
#include <cstring>

struct User
{
    int32_t userId;
    char username[32];
    char passwordHash[64]; // In real system, use proper hashing
    double cashBalance;
    int portfolioRootPage; // Page ID for portfolio B+Tree

    User() : userId(0), cashBalance(0.0), portfolioRootPage(-1)
    {
        memset(username, 0, sizeof(username));
        memset(passwordHash, 0, sizeof(passwordHash));
    }

    static size_t serializedSize() { return 112; } // Approximate size

    void serialize(char *buffer) const
    {
        size_t offset = 0;
        memcpy(buffer + offset, &userId, sizeof(userId));
        offset += sizeof(userId);
        memcpy(buffer + offset, username, sizeof(username));
        offset += sizeof(username);
        memcpy(buffer + offset, passwordHash, sizeof(passwordHash));
        offset += sizeof(passwordHash);
        memcpy(buffer + offset, &cashBalance, sizeof(cashBalance));
        offset += sizeof(cashBalance);
        memcpy(buffer + offset, &portfolioRootPage, sizeof(portfolioRootPage));
    }

    static User deserialize(const char *buffer)
    {
        User user;
        size_t offset = 0;

        memcpy(&user.userId, buffer + offset, sizeof(user.userId));
        offset += sizeof(user.userId);
        memcpy(user.username, buffer + offset, sizeof(user.username));
        offset += sizeof(user.username);
        memcpy(user.passwordHash, buffer + offset, sizeof(user.passwordHash));
        offset += sizeof(user.passwordHash);
        memcpy(&user.cashBalance, buffer + offset, sizeof(user.cashBalance));
        offset += sizeof(user.cashBalance);
        memcpy(&user.portfolioRootPage, buffer + offset, sizeof(user.portfolioRootPage));

        return user;
    }
};