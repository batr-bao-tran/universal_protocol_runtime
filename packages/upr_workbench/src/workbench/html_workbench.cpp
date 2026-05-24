#include "universal_protocol_runtime/workbench/html_workbench.hpp"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace universal_protocol_runtime {
namespace {

std::string html_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::string hex_bytes(const std::vector<std::byte>& bytes) {
  std::ostringstream stream;
  for (size_t index = 0; index < bytes.size(); ++index) {
    if (index != 0) {
      stream << ' ';
    }
    stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(std::to_integer<unsigned char>(bytes[index])) << std::nouppercase << std::dec;
  }
  return stream.str();
}

std::string ascii_bytes(const std::vector<std::byte>& bytes) {
  std::string ascii;
  ascii.reserve(bytes.size());
  for (const std::byte byte : bytes) {
    const auto value = std::to_integer<unsigned char>(byte);
    ascii.push_back(std::isprint(value) != 0 ? static_cast<char>(value) : '.');
  }
  return ascii;
}

void append(std::string* out, std::string_view text) { out->append(text); }

void append_metric(std::string* out, std::string_view label, std::string_view value) {
  append(out,
         R"(<div class="metric"><div class="metric-label">)" + html_escape(label) +
             "</div><div class=\"metric-value\">" + html_escape(value) + "</div></div>");
}

void append_protocol_definition_section(std::string* out, const ProtocolDefinition& definition) {
  append(out, "<section class=\"panel filterable\"><h2>Authoring Definition</h2>");
  append(out, "<p class=\"section-intro\">Human-authored or discovered draft protocol definitions.</p>");
  append(out, "<div class=\"metric-row\">");
  append_metric(out, "Protocol", definition.name);
  append_metric(out, "Messages", std::to_string(definition.messages.size()));
  append_metric(out, "Structs", std::to_string(definition.structs.size()));
  append(out, "</div>");

  for (const MessageDefinition& message : definition.messages) {
    append(out, "<article class=\"layout-card\"><h3>" + html_escape(message.name) + "</h3>");
    append(out, "<table><thead><tr><th>Field</th><th>Kind</th><th>Width</th><th>Notes</th></tr></thead><tbody>");
    for (const FieldDefinition& field : message.fields) {
      std::string notes;
      if (field.has_expected_unsigned) {
        notes += "expect=" + std::to_string(field.expected_unsigned) + " ";
      }
      if (!field.size_from_field.empty()) {
        notes += "size_from=" + field.size_from_field + " ";
      }
      if (field.fixed_size != 0) {
        notes += "fixed_size=" + std::to_string(field.fixed_size) + " ";
      }
      if (!field.referenced_type.empty()) {
        notes += "ref=" + field.referenced_type + " ";
      }
      append(out,
             "<tr><td>" + html_escape(field.name) + "</td><td>" + html_escape(to_string(field.kind)) + "</td><td>" +
                 html_escape(field.width_bytes != 0 ? std::to_string(field.width_bytes)
                                                    : std::to_string(field.fixed_size)) +
                 "</td><td>" + html_escape(notes.empty() ? "-" : notes) + "</td></tr>");
    }
    append(out, "</tbody></table></article>");
  }
  append(out, "</section>");
}

void append_compiled_protocol_section(std::string* out, const CompiledProtocol& protocol) {
  append(out, "<section class=\"panel filterable\"><h2>Compiled Protocol</h2>");
  append(out, "<p class=\"section-intro\">The immutable runtime view consumed by the decoder.</p>");
  append(out, "<div class=\"metric-row\">");
  append_metric(out, "Protocol", protocol.name());
  append_metric(out, "Fingerprint", std::to_string(protocol.fingerprint()));
  append_metric(out, "Messages", std::to_string(protocol.messages().size()));
  append(out, "</div>");

  for (const CompiledMessage& message : protocol.messages()) {
    append(out, "<article class=\"layout-card\"><h3>" + html_escape(message.name()) + "</h3>");
    append(out, "<div class=\"chip-row\">");
    append(out, "<span class=\"chip\">minimum_size=" + std::to_string(message.minimum_size()) + "</span>");
    append(out,
           "<span class=\"chip\">allow_trailing=" + std::string(message.allow_trailing_bytes() ? "true" : "false") +
               "</span>");
    append(out,
           "<span class=\"chip\">dispatch_prefix=" +
               html_escape(hex_bytes(
                   std::vector<std::byte>(message.dispatch_prefix().begin(), message.dispatch_prefix().end()))) +
               "</span>");
    append(out, "</div>");
    append(out, "<table><thead><tr><th>Field</th><th>Kind</th><th>Width</th><th>Dynamic</th></tr></thead><tbody>");
    for (const CompiledField& field : message.fields()) {
      append(out,
             "<tr><td>" + html_escape(field.name) + "</td><td>" + html_escape(to_string(field.kind)) + "</td><td>" +
                 std::to_string(field.width_bytes != 0 ? field.width_bytes : field.fixed_size) + "</td><td>" +
                 std::string(field.dynamic_size ? "yes" : "no") + "</td></tr>");
    }
    append(out, "</tbody></table></article>");
  }
  append(out, "</section>");
}

void append_discovery_section(std::string* out, const DiscoveryReport& report) {
  append(out, "<section class=\"panel filterable\"><h2>Discovery Report</h2>");
  append(out, "<p class=\"section-intro\">Heuristic protocol discovery over sampled framed payloads.</p>");
  append(out, "<div class=\"metric-row\">");
  append_metric(out, "Protocol", report.protocol_name);
  append_metric(out, "Frames", std::to_string(report.frames_analyzed));
  append_metric(out, "Discarded", std::to_string(report.frames_discarded));
  append_metric(out, "Fingerprint", std::to_string(report.draft_fingerprint));
  append(out, "</div>");

  for (const DiscoveredMessage& message : report.messages) {
    append(out, "<article class=\"layout-card\"><h3>" + html_escape(message.name) + "</h3>");
    append(out, "<div class=\"chip-row\">");
    append(out, "<span class=\"chip\">samples=" + std::to_string(message.sample_count) + "</span>");
    append(out,
           "<span class=\"chip\">size_range=" + std::to_string(message.min_size) + "-" +
               std::to_string(message.max_size) + "</span>");
    append(out, "<span class=\"chip\">prefix=" + html_escape(hex_bytes(message.common_prefix)) + "</span>");
    append(out, "</div>");
    append(out, "<p>" + html_escape(message.strategy_summary) + "</p>");
    append(out, "<table><thead><tr><th>Draft Field</th><th>Kind</th><th>Notes</th></tr></thead><tbody>");
    for (const FieldDefinition& field : message.draft_message.fields) {
      std::string notes;
      if (field.has_expected_unsigned) {
        notes += "expect=" + std::to_string(field.expected_unsigned) + " ";
      }
      if (!field.size_from_field.empty()) {
        notes += "size_from=" + field.size_from_field + " ";
      }
      if (field.fixed_size != 0) {
        notes += "fixed_size=" + std::to_string(field.fixed_size) + " ";
      }
      append(out,
             "<tr><td>" + html_escape(field.name) + "</td><td>" + html_escape(to_string(field.kind)) + "</td><td>" +
                 html_escape(notes.empty() ? "-" : notes) + "</td></tr>");
    }
    append(out, "</tbody></table>");

    if (!message.sample_frames.empty()) {
      append(out, "<div class=\"frame-grid\">");
      for (size_t index = 0; index < message.sample_frames.size(); ++index) {
        append(out,
               "<div class=\"frame-card\"><h4>Sample " + std::to_string(index) + "</h4><pre>" +
                   html_escape(hex_bytes(message.sample_frames[index])) + "</pre><div class=\"ascii\">" +
                   html_escape(ascii_bytes(message.sample_frames[index])) + "</div></div>");
      }
      append(out, "</div>");
    }
    append(out, "</article>");
  }
  append(out, "</section>");
}

void append_samples_section(std::string* out, const std::vector<WorkbenchSampleFrame>& sample_frames) {
  append(out, "<section class=\"panel filterable\"><h2>Sample Frames</h2>");
  append(out, "<div class=\"frame-grid\">");
  for (const WorkbenchSampleFrame& frame : sample_frames) {
    append(out,
           "<article class=\"frame-card\"><h3>" + html_escape(frame.label) + "</h3><pre>" +
               html_escape(hex_bytes(frame.bytes)) + "</pre><div class=\"ascii\">" +
               html_escape(ascii_bytes(frame.bytes)) + "</div></article>");
  }
  append(out, "</div></section>");
}

}  // namespace

StatusOr<std::string> render_workbench_html(const WorkbenchPageInput& input) {
  if (input.definition == nullptr && input.compiled_protocol == nullptr && input.discovery_report == nullptr &&
      input.sample_frames.empty()) {
    return invalid_argument("Workbench rendering requires at least one data source.");
  }

  std::string html;
  append(&html, R"(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">)");
  append(&html, R"(<meta name="viewport" content="width=device-width, initial-scale=1">)");
  append(&html, "<title>" + html_escape(input.title) + "</title>");
  append(
      &html,
      "<style>"
      ":root{--bg:#f6f1e8;--ink:#112033;--muted:#516072;--panel:#fffdf8;--line:#d8cbb6;--accent:#0f766e;--warm:#c08457;"
      "}"
      "body{margin:0;font-family:\"Iowan Old Style\",\"Palatino Linotype\",serif;background:radial-gradient(circle at "
      "top,#fffaf0 0,#f6f1e8 45%,#efe7d9 100%);color:var(--ink);}"
      "header{padding:3rem 2rem 2rem;border-bottom:1px solid "
      "var(--line);background:linear-gradient(135deg,rgba(15,118,110,.08),rgba(192,132,87,.08));}"
      "h1,h2,h3,h4{margin:0 0 .6rem 0;font-weight:700;letter-spacing:.01em;}"
      "p{margin:.4rem 0 1rem 0;line-height:1.55;color:var(--muted);}"
      ".shell{max-width:1200px;margin:0 auto;padding:1.5rem;}"
      ".toolbar{display:flex;gap:1rem;align-items:center;flex-wrap:wrap;margin:1rem 0 1.5rem 0;}"
      ".search{flex:1;min-width:220px;padding:.85rem 1rem;border:1px solid "
      "var(--line);border-radius:999px;background:rgba(255,255,255,.8);font:inherit;}"
      ".metric-row{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:1rem;margin:1rem 0 "
      "1.25rem 0;}"
      ".metric,.panel,.frame-card,.layout-card{background:var(--panel);border:1px solid "
      "var(--line);border-radius:20px;box-shadow:0 14px 40px rgba(17,32,51,.06);}"
      ".metric{padding:1rem 1.1rem;}"
      ".metric-label{font-size:.8rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);}"
      ".metric-value{font-size:1.3rem;margin-top:.35rem;}"
      ".panel{padding:1.2rem 1.2rem 1.4rem;margin:0 0 1.25rem 0;}"
      ".section-intro{max-width:60rem;}"
      ".chip-row{display:flex;flex-wrap:wrap;gap:.5rem;margin:.65rem 0 1rem 0;}"
      ".chip{display:inline-block;padding:.35rem "
      ".7rem;border-radius:999px;background:rgba(15,118,110,.08);color:var(--accent);font-size:.85rem;font-family:"
      "\"SFMono-Regular\",\"Cascadia Mono\",monospace;}"
      ".layout-card,.frame-card{padding:1rem;margin:.9rem 0;}"
      ".frame-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:1rem;}"
      "pre,.ascii,table{font-family:\"SFMono-Regular\",\"Cascadia Mono\",monospace;}"
      "pre{white-space:pre-wrap;word-break:break-word;background:#f8f4ec;padding:.85rem;border-radius:14px;border:1px "
      "solid rgba(216,203,182,.8);margin:.4rem 0;}"
      ".ascii{color:var(--warm);font-size:.9rem;}"
      "table{width:100%;border-collapse:collapse;font-size:.9rem;overflow:hidden;}"
      "th,td{text-align:left;padding:.65rem .55rem;border-bottom:1px solid rgba(216,203,182,.75);vertical-align:top;}"
      "th{font-size:.78rem;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);}"
      ".hidden{display:none !important;}"
      "@media (max-width:720px){header{padding:2rem 1rem 1.3rem;}.shell{padding:1rem;}}"
      "</style>");
  append(&html,
         "<script>"
         "document.addEventListener('DOMContentLoaded',function(){const input=document.getElementById('filter');"
         "if(!input)return;input.addEventListener('input',function(){const q=input.value.toLowerCase();"
         "document.querySelectorAll('.filterable').forEach(function(node){node.classList.toggle('hidden',q&& "
         "!node.textContent.toLowerCase().includes(q));});});});"
         "</script></head><body>");
  append(&html, "<header><div class=\"shell\"><h1>" + html_escape(input.title) + "</h1>");
  append(
      &html,
      "<p>Unified inspection for authoring definitions, discovery output, runtime metadata, and captured frames.</p>");
  append(&html, "</div></header>");
  append(&html, "<main class=\"shell\">");
  append(&html,
         "<div class=\"toolbar\"><input id=\"filter\" class=\"search\" placeholder=\"Filter messages, fields, "
         "strategies, or sample bytes\"></div>");

  if (input.discovery_report != nullptr) {
    append(&html, "<div class=\"metric-row\">");
    append_metric(&html, "Discovery Clusters", std::to_string(input.discovery_report->messages.size()));
    append_metric(&html, "Frames Analyzed", std::to_string(input.discovery_report->frames_analyzed));
    append_metric(&html, "Draft Fingerprint", std::to_string(input.discovery_report->draft_fingerprint));
    append(&html, "</div>");
  } else if (input.compiled_protocol != nullptr) {
    append(&html, "<div class=\"metric-row\">");
    append_metric(&html, "Protocol", input.compiled_protocol->name());
    append_metric(&html, "Fingerprint", std::to_string(input.compiled_protocol->fingerprint()));
    append_metric(&html, "Messages", std::to_string(input.compiled_protocol->messages().size()));
    append(&html, "</div>");
  }

  if (input.definition != nullptr) {
    append_protocol_definition_section(&html, *input.definition);
  }
  if (input.compiled_protocol != nullptr) {
    append_compiled_protocol_section(&html, *input.compiled_protocol);
  }
  if (input.discovery_report != nullptr) {
    append_discovery_section(&html, *input.discovery_report);
  }
  if (!input.sample_frames.empty()) {
    append_samples_section(&html, input.sample_frames);
  }

  append(&html, "</main></body></html>");
  return html;
}

Status write_workbench_html_file(const std::string& path, const WorkbenchPageInput& input) {
  StatusOr<std::string> html = render_workbench_html(input);
  if (!html.ok()) {
    return html.status();
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return io_error("Failed to open workbench output file '" + path + "'.");
  }
  output << html.value();
  output.flush();
  if (!output.good()) {
    return io_error("Failed to write workbench output file '" + path + "'.");
  }
  return Status::ok_status();
}

}  // namespace universal_protocol_runtime
