#pragma once
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <utility>

class chat_db {
public:
    chat_db(std::string const &filename) 
    {
        // does the file exist?
        if (!std::filesystem::exists(filename)) {
            // create the database
            sqlite3_open(filename.c_str(), &db);
            sqlite3_exec(db, "CREATE TABLE notes (id INTEGER PRIMARY KEY, topic TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, chat_id INTEGER);", nullptr, nullptr, nullptr);
        }
        else {
            // open the database
            sqlite3_open(filename.c_str(), &db);
        }
    }

    ~chat_db() {
        sqlite3_close(db);
    }

    void add_note(std::string const &topic, std::string const &content, uint64_t chat_id) {
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "INSERT INTO notes (topic, content, chat_id) VALUES (?, ?, ?);", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, chat_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    int get_note_count(int chat_id) {
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM notes WHERE chat_id = ?;", -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, chat_id);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }

    std::vector<std::pair<std::string,std::string>> get_latest_notes(uint64_t chat_id, int count) {
        std::vector<std::pair<std::string,std::string>> notes;
        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(db, "SELECT topic, content FROM notes WHERE chat_id = ? ORDER BY timestamp DESC LIMIT ?;", -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, chat_id);
        sqlite3_bind_int(stmt, 2, count);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            notes.push_back({
                std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), sqlite3_column_bytes(stmt, 0)),
                std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), sqlite3_column_bytes(stmt, 1))
            });
        }
        sqlite3_finalize(stmt);
        return notes;
    }

private:
    sqlite3 *db;

};
