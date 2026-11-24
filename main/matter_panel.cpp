#include "panel.h"
#include "panel_config.h"

#include <array>
#include <atomic>
#include <cstring>
#include <app/server/Server.h>
#include <crypto/CHIPCryptoPAL.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_openthread.h>
#include <esp_random.h>
#include <lib/support/Base64.h>
#include <nvs_flash.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#include <platform/ThreadStackManager.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/SetupPayload.h>

namespace {
using namespace esp_matter;
using namespace chip::app::Clusters;
using namespace chip::DeviceLayer;

constexpr char tag[] = "panel";
constexpr uint32_t on_off_id = OnOff::Attributes::OnOff::Id;
std::array<uint16_t, panel_config::count> endpoints;
std::atomic<uint8_t> on_mask{0};
std::atomic<NetworkStatus> network{NetworkStatus::Starting};

void initialize_pairing_data()
{
    // The SDK's development provider reads these chip-factory keys. Generate
    // a matching PIN/salt/verifier instead of shipping a shared test passcode.
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open("chip-factory", NVS_READWRITE, &nvs));
    uint32_t pin;
    esp_err_t err = nvs_get_u32(nvs, "pin-code", &pin);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        do { pin = 1 + esp_random() % 99999998; }
        while (!chip::SetupPayload::IsValidSetupPIN(pin));
        uint8_t salt[16], serialized[chip::Crypto::kSpake2p_VerifierSerialized_Length];
        esp_fill_random(salt, sizeof(salt));
        chip::Crypto::Spake2pVerifier verifier;
        ESP_ERROR_CHECK(verifier.Generate(1000, chip::ByteSpan(salt), pin) == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL);
        chip::MutableByteSpan bytes(serialized);
        ESP_ERROR_CHECK(verifier.Serialize(bytes) == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL);
        char salt64[25] = {}, verifier64[133] = {};
        chip::Base64Encode(salt, sizeof(salt), salt64);
        chip::Base64Encode(serialized, bytes.size(), verifier64);
        ESP_ERROR_CHECK(nvs_set_u32(nvs, "discriminator", esp_random() % 4096));
        ESP_ERROR_CHECK(nvs_set_u32(nvs, "iteration-count", 1000));
        ESP_ERROR_CHECK(nvs_set_str(nvs, "salt", salt64));
        ESP_ERROR_CHECK(nvs_set_str(nvs, "verifier", verifier64));
        // PIN is written last: a partial first boot will regenerate the bundle.
        ESP_ERROR_CHECK(nvs_set_u32(nvs, "pin-code", pin));
        ESP_ERROR_CHECK(nvs_commit(nvs));
    } else ESP_ERROR_CHECK(err);
    nvs_close(nvs);
}

esp_err_t attribute_changed(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                            uint32_t attribute_id, esp_matter_attr_val_t *value, void *)
{
    if (type == attribute::POST_UPDATE && cluster_id == OnOff::Id && attribute_id == on_off_id) {
        for (unsigned row = 0; row < endpoints.size(); ++row) {
            if (endpoint_id != endpoints[row]) continue;
            const uint8_t bit = 1U << row;
            if (value->val.b) on_mask.fetch_or(bit);
            else on_mask.fetch_and(uint8_t(~bit));
            break;
        }
    }
    return ESP_OK;
}

esp_err_t identify(identification::callback_type_t, uint16_t endpoint, uint8_t, uint8_t, void *)
{
    ESP_LOGI(tag, "Identify endpoint %u", endpoint);
    return ESP_OK;
}

void update_network_status()
{
    const bool commissioned = chip::Server::GetInstance().GetFabricTable().FabricCount() != 0;
    const bool attached = commissioned && ThreadStackMgr().IsThreadAttached();
    network.store(!commissioned ? NetworkStatus::Pairing :
                  attached ? NetworkStatus::Thread : NetworkStatus::Connecting);
}

void matter_event(const ChipDeviceEvent *, intptr_t)
{
    update_network_status();
}

PanelState panel_state()
{
    return {network.load(), on_mask.load()};
}

void toggle_work(intptr_t row)
{
    update_network_status();
    if (network.load() != NetworkStatus::Thread) return;

    const uint16_t endpoint = endpoints[row];
    auto value = esp_matter_invalid(nullptr);
    auto err = attribute::get_val(attribute::get(endpoint, OnOff::Id, on_off_id), &value);
    if (err == ESP_OK) {
        auto next = esp_matter_bool(!value.val.b);
        err = attribute::update(endpoint, OnOff::Id, on_off_id, &next);
    }
    if (err != ESP_OK) ESP_LOGE(tag, "Toggle endpoint %u: %s", endpoint, esp_err_to_name(err));
}

esp_err_t schedule_toggle(unsigned row)
{
    return PlatformMgr().ScheduleWork(toggle_work, row) == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
}

void read_initial_state(intptr_t)
{
    uint8_t mask = 0;
    for (unsigned row = 0; row < endpoints.size(); ++row) {
        auto value = esp_matter_invalid(nullptr);
        ESP_ERROR_CHECK(attribute::get_val(attribute::get(endpoints[row], OnOff::Id, on_off_id), &value));
        if (value.val.b) mask |= 1U << row;
    }
    on_mask.store(mask);
    update_network_status();
    if (network.load() == NetworkStatus::Pairing) {
        PrintOnboardingCodes(chip::RendezvousInformationFlag::kBLE);
    }
}
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    initialize_pairing_data();

    node::config_t node_config;
    std::strcpy(node_config.root_node.basic_information.node_label, "Desk Control Panel");
    auto *node = node::create(&node_config, attribute_changed, identify);
    ESP_ERROR_CHECK(node ? ESP_OK : ESP_ERR_NO_MEM);

    for (unsigned row = 0; row < endpoints.size(); ++row) {
        endpoint::on_off_plugin_unit::config_t config;
        config.on_off.on_off = false;
        config.on_off_lighting.start_up_on_off = nullptr;
        auto *endpoint = endpoint::on_off_plugin_unit::create(node, &config, ENDPOINT_FLAG_NONE, nullptr);
        ESP_ERROR_CHECK(endpoint ? ESP_OK : ESP_ERR_NO_MEM);
        endpoints[row] = endpoint::get_id(endpoint);
        ESP_LOGI(tag, "Endpoint %u: %s", endpoints[row], panel_config::labels[row]);
    }

    esp_openthread_platform_config_t thread = {
        .radio_config = {.radio_mode = RADIO_MODE_NATIVE},
        .host_config = {.host_connection_mode = HOST_CONNECTION_MODE_NONE},
        .port_config = {.storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10},
    };
    set_openthread_platform_config(&thread);
    ESP_ERROR_CHECK(esp_matter::start(matter_event));
    ESP_ERROR_CHECK(PlatformMgr().ScheduleWork(read_initial_state) == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL);
    run_panel(panel_state, schedule_toggle);
}
