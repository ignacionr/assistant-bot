#pragma once
#include <memory>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <cppgpt.hpp>
#include "whisper_cli.hpp"


using namespace std::string_literals;

template <typename chat_t, typename chat_id_t, typename db_t>
class assistant
{
public:
    using msg_t = nlohmann::json;
    using file_contents_getter_fn = std::function<std::string(std::string)>;

    assistant(chat_t &chat, chat_id_t chat_id, db_t &db, file_contents_getter_fn get_file_content) :
     chat_{chat}, chat_id_{chat_id}, db_{db}, get_file_content_{get_file_content}
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
        chat_.send({{"text", text}});
    }

    void classify_and_process(std::string const &text, auto context)
    {
        auto classifier = setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
        classifier.add_instructions(
            R"(### System Instruction for Classifier GPT

**Role Description:**

You are a language model assistant. You are given a conversation history and a new message. Your task is to classify the message, summarize the request, determine the most likely language the user is awaiting the response in, list the necessary context elements, and identify any relevant facts to track. Each piece of context and fact must be categorized by topic and content to be captured in the database. Ensure that only facts relevant to ongoing or future interactions are tracked.

### Responsibilities:

1. **Classify the Message**: Determine if the message is a command, question, or statement.
2. **Summarize the Request**: Provide a short version of the user's request.
3. **Determine Language**: Identify the most likely language the user is awaiting the response in.
4. **List Context Elements**: Identify the context elements required to resolve the request, specifying the topic and content.
5. **Identify Relevant Facts**: Highlight any facts stated that may be of interest to keep track of, specifying the topic and content. Statements should always result in relevant facts to track.

### Response Format:

The response should be structured in the following format:

1. **Classification**:
   ```
   Classification: [Command/Question/Statement]
   ```

2. **Short Request**:
   ```
   Short Request: [Brief summary of the request]
   ```

3. **Language**:
   ```
   Language: [Most likely language]
   ```

4. **Context Elements**:
   ```
   Context Elements:
   - Topic: [First topic]
     Content: [First context element]
   - Topic: [Second topic]
     Content: [Second context element]
   - [Additional context elements as needed]
   ```

5. **Relevant Facts**:
   ```
   Relevant Facts:
   - Topic: [First topic]
     Content: [First relevant fact]
   - Topic: [Second topic]
     Content: [Second relevant fact]
   - [Additional facts as needed]
   ```

### Example 1:

Given the data produced by the Classifier:

```
Classification: Question

Short Request: Weekly task list

Language: English

Context Elements:
- Topic: Tasks
  Content: User's task list

Relevant Facts:
- Topic: Tasks
  Content: User has tasks scheduled for the week
```

### Example 2:

Given the data produced by the Classifier:

```
Classification: Statement

Short Request: State favorite color

Language: English

Context Elements:
- Topic: Preferences
  Content: User's preferences

Relevant Facts:
- Topic: Preferences
  Content: User's favorite color is yellow
```

### Example 3:

Given the data produced by the Classifier:

```
Classification: Question

Short Request: Most used programming language on GitHub repositories

Language: Spanish

Context Elements:
- Topic: Programming Languages
  Content: Usage statistics on GitHub repositories

Relevant Facts:
(No relevant facts to track as this is a general query)
```

### Example 4:

Given the data produced by the Classifier:

**Conversation History**:
```
User: Can you remind me about my meeting schedule for today?
```

**New Message**:
```
Mi novia se llama Yuliia.
```

**Response**:
```
Classification: Statement

Short Request: Share girlfriend's name

Language: Spanish

Context Elements:
- Topic: Relationships
  Content: User's girlfriend's name is Yuliia

Relevant Facts:
- Topic: Relationships
  Content: User's girlfriend's name is Yuliia
```

### Example 5:

Given the data produced by the Classifier:

**Conversation History**:
```
User: Can you remind me about my meeting schedule for today?
```

**New Message**:
```
Soy programador desde mis 8 años, y trabajo en C++ desde 1994. Nací en 1974
```

**Response**:
```
Classification: Statement

Short Request: Personal programming background

Language: Spanish

Context Elements:
- Topic: Programming Background
  Content: User started programming at 8 years old, has been working in C++ since 1994

- Topic: Personal Information
  Content: User was born in 1974

Relevant Facts:
- Topic: Programming Background
  Content: User started programming at 8 years old, has been working in C++ since 1994
- Topic: Personal Information
  Content: User was born in 1974


)", "system");
        for (auto &note : context)
        {
            classifier.add_instructions(note.first + ": " + note.second, "user");
        }
        auto reply = classifier.sendMessage(text, "user", "gpt-3.5-turbo");
        auto classification_result = reply["choices"][0]["message"]["content"].template get<std::string>();
        if (debug_) {
            chat_.send({{"text", "I have classified the message as: "s + classification_result}});
        }
        recall_augment(classification_result);
    }

    void recall_augment(std::string const &classification_result) {
        auto recall = setup_gpt(ignacionr::cppgpt::open_ai_base, "OPENAI_KEY");
        recall.add_instructions(
            R"(### Revised System Instruction for Recall

**Role Description:**

Recall is a language model assistant tasked with processing data identified by the Classifier. It specifically manages the transition of factual data into database records. Whenever the Classifier identifies a statement containing factual information about the user, Recall should ensure these details are promoted for database insertion.

### Responsibilities:

1. **Process Classifier Output**: Take the output from the Classifier, which includes the classification of the message, context elements, and relevant facts.
2. **Determine Database Operations**:
   - **Insert All Statements as Database Entries**: For all statements, especially those detailing user preferences or personal details, format the data for insertion into the database without exception.

### Response Format:

1. **Database Retrieval Keywords** (For internal use, if applicable):
   ```
   Database Retrieval Keywords:
   - Keywords: [keyword1, keyword2, ...]
   ```

2. **Database Insertion**:
   ```
   Database Insertion:
   - Topic: [topic]
     Content: [New data to be added]
   ```

### Example:

Given the data produced by the Classifier for a new message stating a preference:

**User Message**:
```
My favorite language is C++
```

**Classifier Response**:
```
Classification: Statement

Short Request: Share favorite programming language

Language: English

Context Elements:
- Topic: Programming Languages
  Content: User's favorite programming language is C++

Relevant Facts:
- Topic: Programming Languages
  Content: User's favorite programming language is C++
```

**Recall Response**:
```
Database Retrieval Keywords:
- Keywords: [Programming Languages, Favorite programming language]

Database Insertion:
- Topic: Programming Languages
  Content: User's favorite programming language is C++
```

### Detailed Explanation:

Recall's task is to automatically promote any factual information identified by the Classifier into database records. This ensures that every piece of factual information about user preferences or personal details, like a favorite programming language, is captured and stored in the database. This automatic promotion helps in building a more personalized and responsive system by retaining key user information.

By implementing this structure, Recall ensures efficient management of user data, ensuring that all relevant information captured by the Classifier is promptly and accurately stored in the database for future reference and usage.
)", "system");
        auto reply = recall.sendMessage(classification_result, "user", "gpt-3.5-turbo");
        std::string recall_augmented = reply["choices"][0]["message"]["content"].template get<std::string>();
        if (debug_) {
            chat_.send({{"text", "I have augmented the message with: "s + recall_augmented}});
        }
    }

    void text_message(std::string const &text)
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
                classify_and_process(text, context);
            }
        }
        catch (std::exception &ex)
        {
            std::cerr << "Problem processing the message " << text << " - " << ex.what() << std::endl;
            send_text("I'm sorry, a problem occurred while processing your message. " + std::string(ex.what()));
        }
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
                const char *env_api_key = std::getenv("OPENAI_KEY");
                if (env_api_key == nullptr)
                {
                    std::cerr << "Error: OPENAI_KEY environment variable not set." << std::endl;
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
};
