#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comet/state/sqlite_store.h"

namespace comet::controller {

class InteractionConversationRecordBuilder final {
 public:
  std::vector<comet::InteractionMessageRecord> AppendNewMessageRecords(
      std::vector<comet::InteractionMessageRecord> existing,
      const nlohmann::json& delta_messages,
      const std::string& assistant_text,
      const nlohmann::json& usage,
      const std::string& created_at) const;

  std::vector<comet::InteractionMessageRecord> AssignSessionId(
      std::vector<comet::InteractionMessageRecord> records,
      const std::string& session_id) const;

  std::vector<nlohmann::json> MessagesForArchive(
      const std::vector<comet::InteractionMessageRecord>& records) const;

  std::vector<comet::InteractionMessageRecord> BuildRestoredMessageRecords(
      const std::string& session_id,
      const nlohmann::json& messages_json,
      const std::string& created_at) const;

  std::vector<comet::InteractionSummaryRecord> BuildRestoredSummaryRecords(
      const std::string& session_id,
      const nlohmann::json& summaries_json,
      const std::string& created_at) const;
};

}  // namespace comet::controller
