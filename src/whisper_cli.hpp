#pragma once
#include <cpr/cpr.h>

struct whisper_cli
{
    whisper_cli(const std::string &api_key) : api_key_{api_key} {}

    std::string transcribe_audio(const std::string &file_content, const std::string& mime_type)
    {
        std::cerr << "Transcribing audio file with MIME type: " << mime_type << std::endl;
        // Get the file name from the mime type
        std::string sample_file_name = "sample." + mime_type.substr(mime_type.find('/') + 1);
        // Prepare the multipart form data
        cpr::Multipart multipart{
            {"file", cpr::Buffer{file_content.begin(), file_content.end(), sample_file_name}},
            {"model", "whisper-1"},
            {"response_format", "text"}
            };

        // Make the POST request
        cpr::Response r = cpr::Post(
            cpr::Url{"https://api.openai.com/v1/audio/transcriptions"},
            cpr::Bearer{api_key_},
            multipart);

        // Check for errors
        if (r.status_code != 200)
        {
            throw std::runtime_error("Trasncription failed with status code: " + std::to_string(r.status_code));
        }

        // Return the response text
        return r.text;
    }

private:
    std::string api_key_;
};