#include "log_window.h"

#include <imgui.h>
#include <spdlog/details/log_msg.h>

namespace {
std::shared_ptr<ImGuiLogSink> g_sink;
bool g_log_window_open = false;
bool g_autoscroll = true;
} // namespace

void ImGuiLogSink::sink_it_(const spdlog::details::log_msg &msg) {
  spdlog::memory_buf_t formatted;
  formatter_->format(msg, formatted);
  std::string text(formatted.data(), formatted.size());
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    text.pop_back();

  lines_.push_back({std::move(text), msg.level});
  while (lines_.size() > max_lines_)
    lines_.pop_front();
}

std::vector<ImGuiLogSink::Entry> ImGuiLogSink::snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::vector<Entry>(lines_.begin(), lines_.end());
}

void ImGuiLogSink::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  lines_.clear();
}

void initLogWindow(std::shared_ptr<spdlog::logger> logger) {
  if (g_sink)
    return; // already initialised

  g_sink = std::make_shared<ImGuiLogSink>();
  g_sink->set_level(spdlog::level::trace);

  auto target = logger ? logger : spdlog::default_logger();
  if (target) {
    target->sinks().push_back(g_sink);
  }
}

void toggleLogWindow() { g_log_window_open = !g_log_window_open; }
bool isLogWindowOpen() { return g_log_window_open; }
void setLogWindowOpen(bool open) { g_log_window_open = open; }

static ImVec4 colorForLevel(spdlog::level::level_enum lvl) {
  switch (lvl) {
  case spdlog::level::trace:
    return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
  case spdlog::level::debug:
    return ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
  case spdlog::level::info:
    return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
  case spdlog::level::warn:
    return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
  case spdlog::level::err:
    return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
  case spdlog::level::critical:
    return ImVec4(1.0f, 0.15f, 0.15f, 1.0f);
  default:
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  }
}

void drawLogWindow() {
  if (!g_log_window_open || !g_sink)
    return;

  ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Log", &g_log_window_open)) {
    ImGui::End();
    return;
  }

  if (ImGui::Button("Clear")) {
    g_sink->clear();
  }
  ImGui::SameLine();
  ImGui::Checkbox("Autoscroll", &g_autoscroll);
  ImGui::SameLine();
  ImGui::TextDisabled("(also written to logs/3dco+.log in the data directory)");

  ImGui::Separator();

  ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

  auto entries = g_sink->snapshot();
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(entries.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const auto &e = entries[static_cast<size_t>(i)];
      ImGui::PushStyleColor(ImGuiCol_Text, colorForLevel(e.level));
      ImGui::TextUnformatted(e.text.c_str());
      ImGui::PopStyleColor();
    }
  }

  if (g_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f)
    ImGui::SetScrollHereY(1.0f);

  ImGui::EndChild();
  ImGui::End();
}
