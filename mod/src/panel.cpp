// F10: the co-op panel (after the Black co-op's CO-OP page), and a status line while a session runs
#include <cstdio>
#include <string>

#include <imgui.h>

#include "mod.h"
#include "net/session.h"
#include "settings.h"
#include "ui.h"

namespace mod::ui {

namespace {

void Section(const char* title) {
    ImGui::Spacing();
    ImGui::SeparatorText(title);
}

void DrawCoop() {
    coop::Session session = coop::GetSession();
    static char address[128]{};
    static int port = 0;
    if (!port) {
        std::snprintf(address, sizeof address, "%s", Settings().join_ip.c_str());
        port = int(session.port);
    }
    ImGui::TextUnformatted(session.status.c_str());
    if (session.running) ImGui::TextUnformatted(session.role == coop::Role::Host ? "You are the host" : "You joined a friend");
    if (!session.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", session.error.c_str());
        ImGui::PopStyleColor();
    }
    static char nickname[coop::kNicknameBytes]{};
    static bool nickname_set = false;
    if (!nickname_set) {
        std::snprintf(nickname, sizeof nickname, "%s", session.nickname.c_str());
        nickname_set = true;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    bool enter = ImGui::InputText("Nickname", nickname, sizeof nickname, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemDeactivatedAfterEdit() || enter) {
        coop::RequestNickname(nickname);
        std::snprintf(nickname, sizeof nickname, "%s", coop::CleanNickname(nickname).c_str());
    }
    ImGui::Text("Players: %d / %d", session.players, session.capacity);
    for (const auto& member : session.members)
        ImGui::BulletText("%s%s%s", member.nickname.c_str(), member.local ? " (you)" : "", member.host ? " (host)" : "");

    Section("ONLINE WITH FRIENDS");
    static char room_code[24]{};
    if (session.online && !session.room_code.empty()) {
        ImGui::Text("Room code: %s", session.room_code.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Copy code")) ImGui::SetClipboardText(session.room_code.c_str());
        ImGui::TextDisabled("Send this code to your friend.");
    }
    if (!session.online_available) ImGui::TextWrapped("Online rooms need coop-online.toml beside MC3Coop.");
    ImGui::BeginDisabled(!session.online_available || session.running || session.pending);
    if (ImGui::Button("Create online room")) coop::RequestOnline(coop::Role::Host);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    ImGui::InputText("##code", room_code, sizeof room_code, ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::BeginDisabled(!room_code[0]);
    if (ImGui::Button("Join by code")) coop::RequestOnline(coop::Role::Join, room_code);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    Section("LAN / DIRECT IP");
    ImGui::BeginDisabled(session.running || session.pending);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::InputInt("UDP port", &port, 0);
    bool valid_port = port > 0 && port <= 65535;
    auto save = [&](const std::string& host) {
        Settings().coop_port = uint32_t(port);
        Settings().join_ip = host;
        SaveSettings();
    };
    ImGui::BeginDisabled(!valid_port);
    if (ImGui::Button("Host")) {
        save(address);
        coop::RequestStart(coop::Role::Host, {}, uint16_t(port));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(session.searching);
    if (ImGui::Button("Find LAN games")) coop::RequestSearch(uint16_t(port));
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::InputText("##host", address, sizeof address);
    ImGui::SameLine();
    ImGui::BeginDisabled(!address[0]);
    if (ImGui::Button("Join IP")) {
        save(address);
        coop::RequestStart(coop::Role::Join, address, uint16_t(port));
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (session.searching) ImGui::TextUnformatted("Searching...");
    for (const auto& server : session.servers) {
        std::string endpoint = server.address + ":" + std::to_string(server.port);
        ImGui::PushID(endpoint.c_str());
        ImGui::BulletText("%s  %s", server.name.c_str(), endpoint.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(server.busy);
        if (ImGui::Button(server.busy ? "Full" : "Join")) {
            port = server.port;
            std::snprintf(address, sizeof address, "%s", server.address.c_str());
            save(server.address);
            coop::RequestStart(coop::Role::Join, server.address, server.port);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (!valid_port) ImGui::TextUnformatted("Enter a port from 1 to 65535.");

    Section("SESSION");
    ImGui::BeginDisabled(!session.running);
    if (ImGui::Button("Take me to my partner (F8)")) coop::RequestRescue();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(session.pending || (!session.running && !session.searching));
    if (ImGui::Button("Disconnect")) coop::RequestStop();
    ImGui::EndDisabled();
    ImGui::TextDisabled("F10 opens and closes this panel.");
}

}  // namespace

void DrawPanel(bool open) {
    if (coop::Networked()) {
        coop::Remote r;
        char line[96];
        if (coop::PartnerCar(r)) std::snprintf(line, sizeof line, "%s | %s", coop::Status(), r.nickname.c_str());
        else std::snprintf(line, sizeof line, "%s", coop::Status());
        ImGui::GetForegroundDrawList()->AddText(ImVec2(12, 10), IM_COL32(255, 220, 90, 230), line);
    }
    if (!open) return;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    bool keep = true;
    if (ImGui::Begin("MC3 CO-OP  (F10)", &keep, ImGuiWindowFlags_NoCollapse)) DrawCoop();
    ImGui::End();
    if (!keep) Toggle();
}

}  // namespace mod::ui
