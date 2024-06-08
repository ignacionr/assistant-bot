#include <gtest/gtest.h>
#include "../src/assistant.hpp"  // Include the path to the header file containing the `assistant` class.
#include <vector>
#include <string>

using namespace std::string_literals;

// Define a test fixture class for the assistant tests.
class AssistantTest : public ::testing::Test {
protected:
    // SetUp and TearDown can be defined here if needed.
    virtual void SetUp() {}
    virtual void TearDown() {}

    // Helper function to capture section and item outputs.
    static void SectionItemSink(std::vector<std::pair<std::string, std::string>>& collector, const std::string& section_title, const std::string& item) {
        collector.emplace_back(section_title, item);
    }
};

// Test case for parse_sections.
TEST_F(AssistantTest, ParseSections) {
    std::string testInput = 
        "```\n"
        "Database Insertion:\n"
        "- Topic: Test\n"
        "- Content: This is a test entry\n"
        "User Feedback:\n"
        "- Comment: Excellent\n"
        "- Comment: Needs improvement\n"
        "```\n";

    std::vector<std::pair<std::string, std::string>> collectedItems;
    auto lambda = [&collectedItems](const std::string& section, const std::string& item) {
        SectionItemSink(collectedItems, section, item);
    };

    assistant<void*, int, void*>::parse_sections(testInput, lambda);

    // Verify that sections and items are correctly parsed.
    EXPECT_EQ(4, collectedItems.size());
    EXPECT_EQ(std::make_pair("Database Insertion"s, "Topic: Test"s), collectedItems[0]);
    EXPECT_EQ(std::make_pair("Database Insertion"s, "Content: This is a test entry"s), collectedItems[1]);
    EXPECT_EQ(std::make_pair("User Feedback"s, "Comment: Excellent"s), collectedItems[2]);
    EXPECT_EQ(std::make_pair("User Feedback"s, "Comment: Needs improvement"s), collectedItems[3]);
}

// Test case for parse_sections with multiline items.
TEST_F(AssistantTest, ParseSectionsMultilineItems) {
    std::string testInput = 
        "Meeting Notes:\n"
        "- Topic: Review\n"
        "- Details: Discussed the following points:\n"
        "  - Budget adjustments\n"
        "  - Project timelines\n"
        "  - Resource allocations\n"
        "Action Items:\n"
        "- Prepare budget proposal by next week\n"
        "- Schedule follow-up meeting with stakeholders\n";

    std::vector<std::pair<std::string, std::string>> collectedItems;
    auto lambda = [&collectedItems](const std::string& section, const std::string& item) {
        SectionItemSink(collectedItems, section, item);
    };

    // Call parse_sections with the multiline test input.
    assistant<void*, int, void*>::parse_sections(testInput, lambda);

    // Verify that sections and multiline items are correctly parsed.
    EXPECT_EQ(4, collectedItems.size());
    EXPECT_EQ(std::make_pair("Meeting Notes"s, "Topic: Review"s), collectedItems[0]);
    EXPECT_EQ(std::make_pair("Meeting Notes"s, "Details: Discussed the following points:\n  - Budget adjustments\n  - Project timelines\n  - Resource allocations"s), collectedItems[1]);
    EXPECT_EQ(std::make_pair("Action Items"s, "Prepare budget proposal by next week"s), collectedItems[2]);
    EXPECT_EQ(std::make_pair("Action Items"s, "Schedule follow-up meeting with stakeholders"s), collectedItems[3]);
}

// Test case for parse_sections with single section and single multiline item.
TEST_F(AssistantTest, ParseSectionsSingleMultilineItem) {
    std::string testInput =  "```\nDatabase Insertion:\n- Topic: Nickname\n  Content: User is called \"INZ\" in Asia\n```\n";

    std::vector<std::pair<std::string, std::string>> collectedItems;
    auto lambda = [&collectedItems](const std::string& section, const std::string& item) {
        SectionItemSink(collectedItems, section, item);
    };

    // Call parse_sections with the single multiline item test input.
    assistant<void*, int, void*>::parse_sections(testInput, lambda);

    // Verify that the section and the single multiline item are correctly parsed.
    EXPECT_EQ(1, collectedItems.size());
    EXPECT_EQ(std::make_pair("Database Insertion"s, "Topic: Nickname\n  Content: User is called \"INZ\" in Asia"s), collectedItems[0]);
}
