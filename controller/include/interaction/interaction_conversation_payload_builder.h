#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "comet/state/sqlite_store.h"

namespace comet::controller {

class InteractionConversationPayloadBuilder final {
 public:
  nlohmann::json ParseJsonOr(
      const std::string& text,
      const nlohmann::json& fallback) const;

  nlohmann::json ParseJsonObject(const std::string& text) const;

  std::string JsonString(const nlohmann::json& value) const;

  int EstimateTokensForJson(const nlohmann::json& value) const;

  std::vector<nlohmann::json> MessageRecordsToJson(
      const std::vector<comet::InteractionMessageRecord>& records) const;

  std::size_t CommonPrefixLength(
      const std::vector<nlohmann::json>& stored_messages,
      const nlohmann::json& incoming_messages) const;

  nlohmann::json BuildPromptMessages(
      const std::vector<comet::InteractionSummaryRecord>& summaries,
      const std::vector<nlohmann::json>& stored_messages,
      const nlohmann::json& delta_messages) const;

  std::vector<comet::InteractionSummaryRecord> BuildSummaryRecords(
      const std::string& session_id,
      const std::vector<nlohmann::json>& all_messages,
      const nlohmann::json& context_state,
      const std::string& created_at,
      int max_model_len,
      const nlohmann::json& prompt_messages) const;

  nlohmann::json BuildSessionSummaryPayload(
      const comet::InteractionSessionRecord& session,
      std::size_t message_count,
      std::size_t summary_count) const;

  nlohmann::json BuildSessionMessagesPayload(
      const std::vector<comet::InteractionMessageRecord>& messages) const;

  nlohmann::json BuildSessionSummariesPayload(
      const std::vector<comet::InteractionSummaryRecord>& summaries) const;

 private:
  std::string TrimCopy(const std::string& value) const;

  bool JsonEqual(
      const nlohmann::json& left,
      const nlohmann::json& right) const;

  nlohmann::json TailMessagesWithDelta(
      const std::vector<nlohmann::json>& stored_messages,
      const nlohmann::json& delta_messages) const;

  std::string ExcerptText(
      const nlohmann::json& message,
      std::size_t max_bytes = 240) const;

  nlohmann::json BuildSummaryJson(
      const std::vector<nlohmann::json>& messages,
      const nlohmann::json& context_state,
      int turn_range_start,
      int turn_range_end) const;

  std::string BuildSummarySystemInstruction(
      const std::vector<comet::InteractionSummaryRecord>& summaries) const;
};

}  // namespace comet::controller
