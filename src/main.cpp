#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include <nlohmann/json.hpp>
#include <cpptbot.hpp>
#include "assistant.hpp"
#include "chatdb.hpp"

int main(int argc, char *argv[])
{
    chat_db db{"notes.db"};
    // Read API key from environment variable
    const char *env_api_key = std::getenv("TBOT_KEY");
    if (env_api_key == nullptr)
    {
        std::cerr << "Error: TBOT_SAMPLE_KEY environment variable not set." << std::endl;
        return 1;
    }
    using assistant_t = assistant<ignacionr::chat<nlohmann::json>, uint64_t, chat_db>;
    std::vector<std::unique_ptr<assistant_t>> assistants;
    ignacionr::cpptbot bot{env_api_key};

    if (argc > 2)
    {
        uint64_t chat_id = std::stoull(argv[1]);
        std::string message = argv[2];
        nlohmann::json json{{"text", message}};
        bot.sendMessage(chat_id, json);
    }
    else if (argc == 2) {
        // show help
        std::cout << "Usage: " << argv[0] << " <chat_id> <message>" << std::endl;
    }
    else
    {
        std::jthread purge_old_assistants([&assistants] {
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::minutes(1));
                auto now = std::chrono::system_clock::now();
                auto it = std::remove_if(assistants.begin(), assistants.end(), [&now](auto &a) {
                    auto expired = std::chrono::duration_cast<std::chrono::minutes>(now - a->last_updated()).count() > 5;
                    if (expired) a->send_text("👋");
                    return expired;
                });
                assistants.erase(it, assistants.end());
            }
        });

        bot.poll([&assistants, &db, &bot](auto &chat, auto chat_id)
        {
            auto a = std::make_unique<assistant_t>(chat, chat_id, db, [&bot](std::string const &file_id) {
                std::cerr << "Getting file contents for file_id: " << file_id << std::endl;
                return bot.get_file_contents(file_id);
            });
            assistants.emplace_back(std::move(a)); 
        });
    }
}
