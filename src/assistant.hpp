#pragma once
#include <memory>
#include <iostream>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <cppgpt.hpp>

template <typename chat_t>
class assistant
{
public:
    using msg_t = nlohmann::json;

    assistant(chat_t &chat) : chat_{chat}
    {
        // Read API key from environment variable
        const char *env_api_key = std::getenv("OPENAI_KEY");
        if (env_api_key == nullptr)
        {
            std::cerr << "Error: OPENAI_KEY environment variable not set." << std::endl;
            throw std::runtime_error("OPENAI_KEY environment variable not set.");
        }
        std::string api_key = env_api_key;

        // Initialize cppgpt with the API key from the environment variable
        gpt_ = std::make_unique<ignacionr::cppgpt>(api_key);
        init_chat();
        chat.receive([this](msg_t msg) { (*this)(msg);});
    }

    void init_chat() {
        std::string chat_ref = "You are a helpful, cheerful assistant named Chloe. We are starting a chat with the user: " + chat_.info.dump();
        gpt_->add_instructions(chat_ref);
    }

    void text_message(std::string text) {
        if (text == "/clear")
        {
            gpt_->clear();
            init_chat();
            return;
        }
        else if (text == "/model") {
            chat_.send("I'm currently using the " + model_name_ + " model.");
            return;
        }
        else if (text.find("/model ") == 0) {
            std::string new_model = text.substr(7);
            if (new_model == "gpt-3.5-turbo" || new_model == "gpt-4.0-turbo") {
                model_name_ = new_model;
                chat_.send("I'm now using the " + model_name_ + " model.");
            } else {
                chat_.send("I'm sorry, I don't recognize that model. I'm currently using the " + model_name_ + " model.");
            }
            return;
        }
        auto gpt_reply = gpt_->sendMessage(text, "user", model_name_);
        std::string text_reply;
        try
        {
            // Access and print GPT's reply
            text_reply = gpt_reply["choices"][0]["message"]["content"];
            std::cout << "GPT replied: " << text_reply << std::endl;
        }
        catch (std::exception &ex)
        {

            std::cerr << "Problem using the response " << gpt_reply.dump() << " - " << ex.what() << std::endl;
            text_reply = "I'm sorry, I don't know how to respond to that.";
        }
        nlohmann::json response = {{"text", text_reply}};
        chat_.send(response);
    }

    void operator()(msg_t msg)
    {
        // looks like: {"chat":{"first_name":"Игнасио","id":6885715531,"last_name":"Родригэз","type":"private"},"date":1716971494,"from":{"first_name":"Игнасио","id":6885715531,"is_bot":false,"language_code":"en","last_name":"Родригэз"},"message_id":253,"text":"Hahaha"}
        std::cerr << "obtained: " << msg.dump() << std::endl;
        if (msg.find("text") != msg.end())
        {
            std::string text = msg["text"];
            text_message(text);
        }
        else if (msg.find("location") != msg.end())
        {
            std::string text = "my current location is " + 
                std::to_string(msg["location"]["latitude"].get<double>()) + ", " + 
                std::to_string(msg["location"]["longitude"].get<double>()) + 
                "; give me as much detail as you can. What country am I at? Which city is this? If you have any other information, please share it with me.";
            text_message(text);
        }
        else
        {
            chat_.send("I'm sorry, I don't know how to respond to that.");
        }
    }

private:
    chat_t &chat_;
    std::unique_ptr<ignacionr::cppgpt> gpt_;
    std::string model_name_ = "gpt-3.5-turbo";
};
