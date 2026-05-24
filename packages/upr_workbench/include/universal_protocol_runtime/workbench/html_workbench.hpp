#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_WORKBENCH_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_WORKBENCH__HTML_WORKBENCH_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_WORKBENCH_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_WORKBENCH__HTML_WORKBENCH_HPP_

#include <string>
#include <vector>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "universal_protocol_runtime/discovery/protocol_discovery.hpp"
#include "universal_protocol_runtime/pdl/protocol_definition.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Labeled byte frame shown in the generated HTML workbench.
 */
struct WorkbenchSampleFrame {
  std::string label;
  std::vector<std::byte> bytes;
};

/**
 * @brief Input bundle used to render a workbench HTML page.
 */
struct WorkbenchPageInput {
  std::string title = "UPR Workbench";
  const ProtocolDefinition* definition = nullptr;
  const CompiledProtocol* compiled_protocol = nullptr;
  const DiscoveryReport* discovery_report = nullptr;
  std::vector<WorkbenchSampleFrame> sample_frames;
};

/**
 * @brief Renders a static HTML workbench page from UPR artifacts.
 * @param input Workbench content and metadata.
 * @return HTML document string or an error status.
 */
StatusOr<std::string> render_workbench_html(const WorkbenchPageInput& input);
/**
 * @brief Writes a rendered workbench HTML page to disk.
 * @param path Output HTML file path.
 * @param input Workbench content and metadata.
 * @return Success or an error status.
 */
Status write_workbench_html_file(const std::string& path, const WorkbenchPageInput& input);

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_WORKBENCH_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_WORKBENCH__HTML_WORKBENCH_HPP_
