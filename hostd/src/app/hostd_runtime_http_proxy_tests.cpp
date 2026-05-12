#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "app/hostd_runtime_http_proxy.h"

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string Chunk(const std::string& data) {
  std::ostringstream stream;
  stream << std::hex << data.size() << "\r\n" << data << "\r\n";
  return stream.str();
}

void TestChunkedRuntimeResponseIsDecoded() {
  const std::string first_frame =
      "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n";
  const std::string done_frame = "data: [DONE]\n\n";
  const std::string response_text =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Connection: close\r\n"
      "\r\n"
      + Chunk(first_frame) + Chunk(done_frame) + "0\r\n\r\n";

  const auto response =
      naim::hostd::HostdRuntimeHttpProxy::ParseHttpResponse(response_text);

  Expect(response.status_code == 200, "status should be preserved");
  Expect(
      response.content_type == "text/event-stream",
      "content type should be preserved");
  Expect(
      response.body == first_frame + done_frame,
      "chunk framing should be removed from SSE body");
  Expect(
      response.headers.find("transfer-encoding") == response.headers.end(),
      "decoded proxy response should not retain transfer-encoding");
}

void TestKnowledgeVaultPolicyAllowsDeleteRoutes() {
  Expect(
      naim::hostd::HostdRuntimeHttpProxy::IsAllowedProxyPath(
          naim::hostd::HostdRuntimeProxyPolicy::KnowledgeVault,
          "DELETE",
          "/v1/blocks/kv-delete-test"),
      "knowledge vault proxy should allow block delete routes");
  Expect(
      naim::hostd::HostdRuntimeHttpProxy::IsAllowedProxyPath(
          naim::hostd::HostdRuntimeProxyPolicy::KnowledgeVault,
          "DELETE",
          "/v1/relations/rel-delete-test"),
      "knowledge vault proxy should allow relation delete routes");
  Expect(
      naim::hostd::HostdRuntimeHttpProxy::IsAllowedProxyPath(
          naim::hostd::HostdRuntimeProxyPolicy::KnowledgeVault,
          "DELETE",
          "/v1/sources/source-delete-test"),
      "knowledge vault proxy should allow source delete routes");
}

void TestSkillsPolicyAllowsSyncRoute() {
  Expect(
      naim::hostd::HostdRuntimeHttpProxy::IsAllowedProxyPath(
          naim::hostd::HostdRuntimeProxyPolicy::Skills,
          "POST",
          "/v1/sync"),
      "skills proxy should allow explicit sync route");
}

void TestVoicePolicyAllowsTranscriptionRoutes() {
  Expect(
      naim::hostd::HostdRuntimeHttpProxy::IsAllowedProxyPath(
          naim::hostd::HostdRuntimeProxyPolicy::Voice,
          "GET",
          "/health"),
      "voice proxy should allow health route");
  Expect(
      naim::hostd::HostdRuntimeHttpProxy::IsAllowedProxyPath(
          naim::hostd::HostdRuntimeProxyPolicy::Voice,
          "POST",
          "/v1/transcribe"),
      "voice proxy should allow v1 transcription route");
  Expect(
      naim::hostd::HostdRuntimeHttpProxy::IsAllowedProxyPath(
          naim::hostd::HostdRuntimeProxyPolicy::Voice,
          "POST",
          "/api/asr/transcribe"),
      "voice proxy should allow ASR transcription route");
}

}  // namespace

int main() {
  TestChunkedRuntimeResponseIsDecoded();
  TestKnowledgeVaultPolicyAllowsDeleteRoutes();
  TestSkillsPolicyAllowsSyncRoute();
  TestVoicePolicyAllowsTranscriptionRoutes();
  std::cout << "hostd runtime HTTP proxy tests passed\n";
  return 0;
}
