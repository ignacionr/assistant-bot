#pragma once
#include <memory>
#include <ranges>
#include <iterator>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>
#include <thread>
#include <regex>
#include <nlohmann/json.hpp>
#include <cppgpt.hpp>
#include "whisper_cli.hpp"
#include "system-classifier.hpp"
#include "system-recall.hpp"
#include "system-executor.hpp"
#include "system-responder.hpp"
#include "system-summary.hpp"

using namespace std::string_literals;

template <typename chat_t, typename chat_id_t, typename db_t>
class assistant
{
public:
  using context_t = std::vector<std::pair<std::string,std::string>>;
  using msg_t = nlohmann::json;
  using file_contents_getter_fn = std::function<std::string(std::string)>;

  assistant(chat_t &chat, chat_id_t chat_id, db_t &db, file_contents_getter_fn get_file_content) : chat_{chat}, chat_id_{chat_id}, db_{db}, get_file_content_{get_file_content}
  {
    // setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
    chat.receive([this](msg_t msg)
                 { (*this)(msg); });
    last_updated_ = std::chrono::system_clock::now();
  }

  ignacionr::cppgpt setup_gpt(std::string const &base_url, std::string const &environment_variable)
  {
    // Read API key from environment variable
    const char *env_api_key = std::getenv(environment_variable.c_str());
    if (env_api_key == nullptr)
    {
      std::cerr << "Error: OPENAI_KEY environment variable not set." << std::endl;
      throw std::runtime_error("OPENAI_KEY environment variable not set.");
    }
    api_key_ = env_api_key;

    return ignacionr::cppgpt(api_key_, base_url);
  }

  static std::pair<std::string, std::string> exec(const char *cmd)
  {
    std::array<char, 128> buffer;
    std::string stdout_result;
    std::string stderr_result;
    std::unique_ptr<FILE, int (*)(FILE *)> pipe(popen((std::string(cmd) + " 2>&1").c_str(), "r"), pclose);
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
    std::lock_guard<std::mutex> lock(conversation_mutex);
    chat_.send({{"text", text}});
    conversation.push_back({"assistant", std::string{text}});
  }

  std::string execute_scripts(std::string const &text, context_t &context)
  {
    auto executor = setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
    std::string executor_instructions = system_executor;
    // add the context to the executor instructions
    executor_instructions += "\n\nContext Information:\n";
    for (auto &note : context)
    {
      executor_instructions += note.second + ": " + note.first + "\n";
    }
    executor.add_instructions(executor_instructions, "system");
    for (auto &note : context)
    {
      executor.add_instructions(note.first + ": " + note.second, "user");
    }
    auto reply = executor.sendMessage(text, "user", "gpt-3.5-turbo");
    auto response = reply["choices"][0]["message"]["content"].template get<std::string>();
    if (debug_)
    {
      chat_.send({{"text", "I have executed the following scripts: "s + response}});
    }
    // in the response, find the scripts to execute
    // std::regex script_regex(R"(\s*-?\s*`?(.+)`?)");
    // std::regex script_regex(R"((```(?:bash)?\s*([^`]+)```|`([^`]+)`|([^`\s].*)))");
    // std::regex script_regex(R"((```(?:bash)?\s*([^`]+?)```|---begin quote\s*`([^`]+?)`\s*---end quote|`([^`]+?)`|([^`\s].*)))");
    std::regex script_regex(R"((```(?:bash)?\s*([^`]+)```|`([^`]+)`|\s*(.*)))");
    std::smatch script_match;
    if (std::regex_search(response, script_match, script_regex))
    {
      std::string script_source = 
        script_match[2].matched ? script_match[2].str() :
        script_match[3].matched ? script_match[3].str() :
        script_match[4].matched ? script_match[4].str() :
        std::string{};
      if (!script_source.empty())
      {
        if (debug_)
        {
          chat_.send({{"text", "I have found the following script to execute: "s + script_source}});
        }
        auto [stdout_result, stderr_result] = exec(script_source.c_str());
        if (debug_)
        {
          chat_.send({{"text", "I have executed the script and obtained the following result: stdout: "s + stdout_result + " stderr: " + stderr_result}});
        }
        context.push_back({"script", script_source});
        context.push_back({"script_result_stdout", stdout_result});
        context.push_back({"script_result_stderr", stderr_result});
      }
    }
    else if (debug_)
    {
      chat_.send({{"text", "I have not found any scripts to execute."}});
    }
    return response;
  }

  std::string classify(std::string const &text, context_t &context)
  {
    auto classifier = setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
    classifier.add_instructions(system_classifier, "system");
    for (auto &note : context)
    {
      classifier.add_instructions(note.first + ": " + note.second, "user");
    }
    auto reply = classifier.sendMessage(text, "user", "gpt-3.5-turbo");
    auto classification_result = reply["choices"][0]["message"]["content"].template get<std::string>();
    if (debug_)
    {
      chat_.send({{"text", "I have classified the message as: "s + classification_result}});
    }
    return classification_result;
  }

  std::string recall_augment(std::string const &classification_result, context_t &context)
  {
    auto recall = setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
    recall.add_instructions(system_recall, "system");
    auto reply = recall.sendMessage(classification_result, "user", "gpt-3.5-turbo");
    std::string recall_augmented = reply["choices"][0]["message"]["content"].template get<std::string>();
    if (debug_)
    {
      chat_.send({{"text", "I have augmented the message with: "s + recall_augmented}});
    }
    
    parse_sections(recall_augmented, [this, &context](std::string const &section_title, std::string const &item) {
      if (debug_) {
        chat_.send({{"text", "I have processed the following section: "s + section_title}});
      }
      if (section_title == "Database Insertion")
      {
        if (debug_) {
          chat_.send({{"text", "I have processed the following item: "s + item}});
        }
        std::regex topic_regex(R"(Topic: (.+))");
        std::regex content_regex(R"(Content: (.+))");
        std::smatch topic_match;
        std::smatch content_match;
        if (std::regex_search(item, topic_match, topic_regex) && std::regex_search(item, content_match, content_regex))
        {
          if (debug_) {
            send_text("I have inserted the following note: "s + topic_match[1].str() + " - " + content_match[1].str());
          }
          db_.add_note(topic_match[1].str(), content_match[1].str(), chat_id_);
        }
      }
      else if (section_title == "Database Retrieval Keywords") {
        if (debug_) {
          send_text("I have processed the following item: "s + item);
        }
        std::regex topic_regex(R"(Keywords: \[(.+)\])");
        std::smatch topic_match;
        if (std::regex_search(item, topic_match, topic_regex)) {
          std::string keywords_list = topic_match[1];
          std::istringstream iss(keywords_list);
          std::string keyword;

          // Extracting each keyword by splitting the list
          while (std::getline(iss, keyword, ',')) {
            // remove leading whitespace, if there is one
            if (keyword.front() == ' ') {
              keyword = keyword.substr(1);
            }
            if (debug_) {
                send_text("I will retrieve notes about: " + keyword);
            }
            auto db_context = db_.search_notes(chat_id_, keyword);
            std::ranges::move(db_context, std::back_inserter(context));
          }
        }
      }
    });
    return recall_augmented;
  }

  std::string summarize(std::string const &text, context_t const &context) {
    ignacionr::cppgpt summarizer = setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
    std::string summary_request = system_summary;

    // add context to the summary_request
    for (const auto& note : context) {
      summary_request += note.second + ": " + note.first + "\n";
    }
    summarizer.add_instructions(summary_request, "system");
    auto reply = summarizer.sendMessage(text, "user", "gpt-3.5-turbo");
    std::string summary = reply["choices"][0]["message"]["content"].template get<std::string>();
    if (debug_) {
      chat_.send({{"text", "Here is the summary: "s + summary}});
    }
    return summary;
  }

  void text_message(std::string const &text) {
    std::thread t([this, text] {
      text_message_execute(text);
    });
    t.detach();
  }

  void text_message_execute(std::string const &text)
  {
    try
    {
      if (text == "/debug")
      {
        debug_ = !debug_;
        chat_.send({{"text", "Debug mode is now " + std::string(debug_ ? "on" : "off")}});
      }
      else if (text == "/help")
      {
        chat_.send({{"text", "You can use the following commands:\n\n"
                             "/debug - Toggle debug mode\n"
                             "/help - Show this help message"}});
      }
      else
      {
        auto context = db_.get_latest_notes(chat_id_, 5);
        // add the current conversation to the context
        {
          std::lock_guard<std::mutex> lock(conversation_mutex);
          for (auto &note : conversation)
          {
            context.push_back(note);
          }
        }
        auto classification = classify(text, context);
        auto recall_response = recall_augment(classification, context);
        if (debug_) {
          // show the full context so far
          std::string context_text = "Context so far:\n";
          for (auto &note : context)
          {
            context_text += note.first + ": " + note.second + "\n";
          }
          chat_.send({{"text", context_text}});
        }
        context.push_back({"classification", classification});
        auto executor_response = execute_scripts(text, context);
        auto summary = summarize(text, context);
        auto responder = setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
        std::string responder_instructions = system_responder;
        responder_instructions += "\n\nContext Information:\n";
        responder_instructions += summary;
        
        responder.add_instructions(responder_instructions, "system");
        auto reply = responder.sendMessage(text, "user", "gpt-3.5-turbo");
        auto response = reply["choices"][0]["message"]["content"].template get<std::string>();
        {
          std::lock_guard<std::mutex> lock(conversation_mutex);
          conversation.push_back({"user", text});
        }
        send_text(response);
      }
    }
    catch (std::exception &ex)
    {
      send_text("I'm sorry, a problem occurred while processing your message. " + std::string(ex.what()));
    }
  }

  void operator()(msg_t msg)
  {
    last_updated_ = std::chrono::system_clock::now();
    // msg looks like: {"chat":{"first_name":"Игнасио","id":6885715531,"last_name":"Родригэз","type":"private"},"date":1716971494,"from":{"first_name":"Игнасио","id":6885715531,"is_bot":false,"language_code":"en","last_name":"Родригэз"},"message_id":253,"text":"Hahaha"}
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
        const char *env_api_key = std::getenv("OPENAI_KEY");
        if (env_api_key == nullptr)
        {
          throw std::runtime_error("OPENAI_KEY environment variable not set.");
        }
        whisper_ = std::make_unique<whisper_cli>(env_api_key);
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

static void parse_sections(std::string text, std::function<void(const std::string&, const std::string&)> section_item_sink)
{
    // if the first line is '```\n', remove it
    if (text.substr(0, 4) == "```\n")
    {
        text = text.substr(4);
    }

    // if the last line is '```\n', remove it
    if (text.substr(text.size() - 4) == "```\n")
    {
        text = text.substr(0, text.size() - 4);
    }

    std::istringstream stream(text);
    std::string line;
    std::string current_section;
    std::string current_item;

    while (std::getline(stream, line))
    {
        if (line.empty())
        {
            continue; // skip empty lines
        }

        if (line.starts_with("- ") || line.starts_with(" - "))
        {
            if (!current_item.empty() && !current_section.empty())
            {
                section_item_sink(current_section, current_item);
            }
            current_item = line.substr(2);
        }
        else if (line.front() != ' ' && line.back() == ':')
        {
            if (!current_item.empty() && !current_section.empty())
            {
                section_item_sink(current_section, current_item);
                current_item.clear();
            }
            current_section = line.substr(0, line.size() - 1);
        }
        else
        {
            if (!current_item.empty())
            {
                current_item += "\n" + line;
            }
        }
    }

    if (!current_item.empty() && !current_section.empty())
    {
        section_item_sink(current_section, current_item);
    }
}

private:
  chat_t &chat_;
  chat_id_t chat_id_;
  db_t &db_;
  std::unique_ptr<whisper_cli> whisper_;
  std::string model_name_ = "gpt-3.5-turbo";
  bool debug_ = false;
  std::string api_key_;
  file_contents_getter_fn get_file_content_;
  std::chrono::time_point<std::chrono::system_clock> last_updated_;
  context_t conversation;
  // a mutex to protect the conversation context
  std::mutex conversation_mutex;
};
