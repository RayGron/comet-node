#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class ModelConversionService {
 public:
  struct Request {
    std::string job_id;
    std::string model_id;
    std::filesystem::path destination_root;
    std::vector<std::string> source_urls;
    std::string detected_source_format;
    std::string desired_output_format;
    std::vector<std::string> quantizations;
    bool keep_base_gguf = true;
    std::filesystem::path staging_directory;
  };

  struct QuantizationRequest {
    std::string job_id;
    std::filesystem::path source_path;
    std::string quantization;
    std::filesystem::path staging_directory;
    std::filesystem::path retained_output_path;
    bool replace_existing = true;
  };

  struct Plan {
    std::filesystem::path destination_root;
    std::filesystem::path staging_directory;
    std::vector<std::filesystem::path> downloaded_source_paths;
    std::filesystem::path staged_base_output_path;
    std::vector<std::string> quantizations;
    std::vector<std::filesystem::path> staged_quantized_output_paths;
    std::vector<std::filesystem::path> retained_output_paths;
  };

  struct QuantizationPlan {
    std::filesystem::path source_path;
    std::filesystem::path staging_directory;
    std::string quantization;
    std::filesystem::path staged_output_path;
    std::filesystem::path retained_output_path;
  };

  struct JobHooks {
    std::function<bool()> stop_requested;
    std::function<void(int)> register_pid;
    std::function<void()> clear_pid;
    std::function<void(
        const std::string& phase,
        const std::string& current_item,
        const std::optional<std::uintmax_t>& bytes_total,
        std::uintmax_t bytes_done)> update_job;
  };

  explicit ModelConversionService(std::filesystem::path repo_root = {});

  static std::string DetectSourceFormat(const std::vector<std::string>& source_urls);
  static std::string NormalizeOutputFormat(const std::string& value);
  static std::vector<std::string> NormalizeQuantizations(
      const std::vector<std::string>& values);
  static std::string DeriveBaseFilename(
      const std::string& model_id,
      const std::filesystem::path& destination_root,
      const std::vector<std::string>& source_urls);

  Plan BuildPlan(const Request& request) const;
  void Execute(const Request& request, const Plan& plan, const JobHooks& hooks) const;
  QuantizationPlan BuildQuantizationPlan(const QuantizationRequest& request) const;
  void ExecuteQuantization(
      const QuantizationRequest& request,
      const QuantizationPlan& plan,
      const JobHooks& hooks) const;

 private:
  struct ToolPaths {
    std::string python_executable;
    std::filesystem::path convert_script_path;
    std::filesystem::path quantize_executable_path;
  };

  class LlamaCppToolLocator {
   public:
    explicit LlamaCppToolLocator(std::filesystem::path repo_root = {});
    ToolPaths Resolve() const;

   private:
    std::filesystem::path repo_root_;
  };

  class HfToGgufPythonConverter {
   public:
    explicit HfToGgufPythonConverter(ToolPaths tool_paths);

    void Convert(
        const Plan& plan,
        const JobHooks& hooks) const;

   private:
    ToolPaths tool_paths_;
  };

  class GgufQuantizationService {
   public:
    explicit GgufQuantizationService(ToolPaths tool_paths);

    void Quantize(
        const Plan& plan,
        const JobHooks& hooks) const;

   private:
    ToolPaths tool_paths_;
  };

  static std::string FilenameFromUrl(const std::string& source_url);
  static std::string Lowercase(const std::string& value);
  static std::string Trim(const std::string& value);
  static std::string StripKnownQuantizationSuffix(const std::string& stem);
  static std::string ShellEscape(const std::string& value);
  static bool FileExistsAndNonEmpty(const std::filesystem::path& path);
  static std::uintmax_t FileSizeOrZero(const std::filesystem::path& path);
  static void EnsureDirectory(const std::filesystem::path& path);
  static void RemovePathIfExists(const std::filesystem::path& path);
  static std::filesystem::path DetectRepoRoot();
  static void RunCommand(
      const std::vector<std::string>& argv,
      const std::filesystem::path& working_directory,
      const JobHooks& hooks,
      const std::string& phase,
      const std::string& current_item,
      const std::filesystem::path& progress_path = {},
      const std::optional<std::uintmax_t>& progress_total = std::nullopt);

  LlamaCppToolLocator tool_locator_;
};
