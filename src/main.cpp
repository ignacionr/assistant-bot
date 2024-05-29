#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include <cpptbot.hpp>

#include "assistant.hpp"

int main() {
    // Read API key from environment variable
    const char* env_api_key = std::getenv("TBOT_KEY");
    if (env_api_key == nullptr)
    {
        std::cerr << "Error: TBOT_SAMPLE_KEY environment variable not set." << std::endl;
        return 1;
    }
    using assistant_t = assistant<ignacionr::chat<nlohmann::json>>;
    std::vector<std::unique_ptr<assistant_t>> assistants;
    ignacionr::cpptbot bot{env_api_key};

    bot.poll([&assistants](auto &chat, auto chat_id) {
        auto a = std::make_unique<assistant_t>(chat);
        assistants.emplace_back(std::move(a));
    });
}
