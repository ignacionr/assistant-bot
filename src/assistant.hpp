#pragma once
#include <memory>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <cppgpt.hpp>
#include "whisper_cli.hpp"

template <typename chat_t>
class assistant
{
public:
    using msg_t = nlohmann::json;
    using file_contents_getter_fn = std::function<std::string(std::string)>;

    assistant(chat_t &chat, file_contents_getter_fn get_file_content) : chat_{chat}, get_file_content_{get_file_content}
    {
        // Read API key from environment variable
        const char *env_api_key = std::getenv("OPENAI_KEY");
        if (env_api_key == nullptr)
        {
            std::cerr << "Error: OPENAI_KEY environment variable not set." << std::endl;
            throw std::runtime_error("OPENAI_KEY environment variable not set.");
        }
        api_key_ = env_api_key;

        // Initialize cppgpt with the API key from the environment variable
        gpt_ = std::make_unique<ignacionr::cppgpt>(api_key_);
        init_chat();
        chat.receive([this](msg_t msg)
                     { (*this)(msg); });
        last_updated_ = std::chrono::system_clock::now();
    }

    void init_chat()
    {
        std::string chat_ref = R"(You are a helpful, expedite, cheerful assistant named Umnaia, who would rather beg for pardon than for permission.
The user can talk to you through Whisper and you will receive the transcription, or they can type.
If at any point you need to use any CLI, you can include in your reply a JSON with a value for "system" -send the JSON on a single line, system will report to you the results, on a separate message that you will receive from system and will not be seen by the user (any important information, you whould tell them).
Using system commands, do keep track of what users start conversations with you, with date, time, and the chat id.
For example, if the user sends you a location with latitude and longitude, such as:
Location: -34.6037,-58.3816
You should immediately send a reply such as:
{"system": "curl "https://nominatim.openstreetmap.org/reverse?format=json&lat=-34.6037&lon=-58.3816"}
Which will give you a JSON with the location details.
By "immediately" I mean that as soon as you know what you can help your chances to be helpful, send the JSON system.
Avoid messages such as "Just a moment.", send the JSON system first, then give all details you can.
Offer that information to the user in a conversational way.

When the user asks for news, they might say something like:
Summarize for me the news in this area.
or
I want the news from Argentina.
You would reply with a system command such as:
{"system": "curl -s \"https://news.google.com/rss/search?hl=en-US&gl=US&ceid=US%3Aen&oc=11&q=Argentina\" | xmlstarlet sel -t -m \"//item/title\" -v . -n"}
And use the results to reply to the user.

If the user asks something like
¿Qué hora es en este momento aquí donde estoy?
You should obtain the current time, for example:
{"system": "date"}
And then translate the output time zone to the place the user is, and reply with the date and time. If the user has mentioned other places, you can also include the time there too.

We are now starting a chat with the user described by: )" + chat_.info.dump() + " greet them in their prefered language.";
        gpt_->add_instructions(chat_ref);
    }

    static std::pair<std::string, std::string> exec(const char *cmd)
    {
        std::array<char, 128> buffer;
        std::string stdout_result;
        std::string stderr_result;
        std::unique_ptr<FILE, int(*)(FILE*)> pipe(popen((std::string(cmd) + " 2>&1").c_str(), "r"), pclose);
        if (!pipe)
        {
            throw std::runtime_error("popen() failed!");
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        {
            stdout_result += buffer.data();
        }
        return {stdout_result, stderr_result};
    }

    void send_text(std::string_view text)
    {
        chat_.send({{"text", text}});
    }
    
    void process_gpt_reply(nlohmann::json gpt_reply)
    {
        if (gpt_reply.find("error") != gpt_reply.end()) {
            std::string error_message = gpt_reply["error"]["message"];
            std::cerr << "GPT error: " << error_message << std::endl;
            send_text(error_message);
        }
        else if (gpt_reply.find("choices") != gpt_reply.end())
        {
            std::string text_reply;
            try
            {
                // Access and print GPT's reply
                text_reply = gpt_reply["choices"][0]["message"]["content"];
                std::cout << "GPT replied: " << text_reply << std::endl;

                // does the text_reply contain a system command?
                auto system_command_pos = text_reply.find("{\"system\":");
                if (system_command_pos != std::string::npos)
                {
                    // consider the command indication a JSON object, ending at the last ocurring "}"
                    auto system_command_end = text_reply.rfind("}");
                    std::string system_command = text_reply.substr(system_command_pos, system_command_end - system_command_pos + 1);
                    // send over the chat, the text except the system command
                    text_reply = text_reply.substr(0, system_command_pos);
                    send_text(text_reply);
                    // parse it as JSON
                    nlohmann::json system_command_json = nlohmann::json::parse(system_command);
                    // extract the command line
                    std::string command = system_command_json["system"];
                    // execute the command
                    std::cerr << "executing system command: " << command << std::endl;
                    if (debug_) {
                        send_text("# " + command);
                    }
                    auto [stdout_result, stderr_result] = exec(command.c_str());
                    std::string system_reply = nlohmann::json{{"stdout", stdout_result}, {"stderr", stderr_result}}.dump();
                    if (debug_) {
                        send_text(system_reply);
                    }
                    // send the system reply back to the model
                    gpt_reply = gpt_->sendMessage(system_reply, "system", model_name_);
                    process_gpt_reply(gpt_reply);
                    return;
                }
            }
            catch (std::exception &ex)
            {
                std::cerr << "Problem using the response " << gpt_reply.dump() << " - " << ex.what() << std::endl;
                text_reply = "I'm sorry, I don't know how to respond to that.";
            }
            send_text(text_reply);
        }
        else
        {
            send_text("I'm sorry, I don't know how to respond to that.");
        }
    }

    void text_message(std::string text)
    {
        if (text == "/clear")
        {
            gpt_->clear();
            init_chat();
            return;
        }
        else if (text == "/model")
        {
            std::cerr << "model_name_: " << model_name_ << std::endl;
            chat_.send({{"text", "I'm currently using the " + model_name_ + " model."}});
            std::cerr << "model reported." << std::endl;
            return;
        }
        else if (text.find("/model ") == 0)
        {
            std::string new_model = text.substr(7);
            model_name_ = new_model;
            chat_.send({{"text", "I'm now using the " + model_name_ + " model."}});
            return;
        }
        else if (text == "/debug")
        {
            debug_ = !debug_;
            chat_.send({{"text", "Debug mode is now " + std::string(debug_ ? "on" : "off")}});
            return;
        }
        else if (text == "/help")
        {
            chat_.send({{"text", "You can use the following commands:\n\n"
                                 "/clear - Clear the conversation history\n"
                                 "/model - Report the current model being used\n"
                                 "/model <model_name> - Change the model being used\n"
                                 "/debug - Toggle debug mode\n"
                                 "/help - Show this help message"}});
            return;
        }
        auto gpt_reply = gpt_->sendMessage(text, "user", model_name_);
        std::cerr << "gpt_reply: " << gpt_reply.dump() << std::endl;
        process_gpt_reply(gpt_reply);
    }

    void operator()(msg_t msg)
    {
        last_updated_ = std::chrono::system_clock::now();
        // msg looks like: {"chat":{"first_name":"Игнасио","id":6885715531,"last_name":"Родригэз","type":"private"},"date":1716971494,"from":{"first_name":"Игнасио","id":6885715531,"is_bot":false,"language_code":"en","last_name":"Родригэз"},"message_id":253,"text":"Hahaha"}
        std::cerr << "obtained: " << msg.dump() << std::endl;
        if (msg.find("text") != msg.end())
        {
            std::string text = msg["text"];
            text_message(text);
        }
        else if (msg.find("location") != msg.end())
        {
            std::string text = "Location: " +
                               std::to_string(msg["location"]["latitude"].get<double>()) + ", " +
                               std::to_string(msg["location"]["longitude"].get<double>()) +
                               "; give me as much detail as you can. What country am I at? Which city is this? If you have any other information, please share it with me.";
            text_message(text);
        }
        else if (msg.find("voice") != msg.end())
        {
            std::string mime_type = msg["voice"]["mime_type"];
            std::string file_id = msg["voice"]["file_id"];
            std::string contents = get_file_content_(file_id);
            if (!whisper_)
            {
                whisper_ = std::make_unique<whisper_cli>(api_key_);
            }
            std::string text = whisper_->transcribe_audio(contents, mime_type);
            std::cerr << "Transcribed audio: " << text << std::endl;
            text_message(text);
        }
        else
        {
            send_text("I'm sorry, I don't know how to respond to that.");
        }
    }

    auto last_updated() const
    {
        return last_updated_;
    }

private:
    chat_t &chat_;
    std::unique_ptr<ignacionr::cppgpt> gpt_;
    std::unique_ptr<whisper_cli> whisper_;
    std::string model_name_ = "gpt-3.5-turbo";
    bool debug_ = false;
    std::string api_key_;
    file_contents_getter_fn get_file_content_;
    std::chrono::time_point<std::chrono::system_clock> last_updated_;
};
