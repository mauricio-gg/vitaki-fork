include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vitaki")
set(VITA_TITLEID  "CHIAKI001")
# TODO: Make it dynamic
set(VITA_VERSION "00.06")

add_executable(${VITA_APP_NAME}.elf
    # Vitaki-fork backend (unchanged)
    src/config.c
    src/context.c
    src/discovery.c
    src/main.c
    src/ui.c
    src/util.c
    src/host.c
    src/controller.c
    src/video.c
    src/audio.c
    src/message_log.c

    # VitaRPS5 Core modules
    src/core/console_registration.c
    src/core/console_storage.c
    src/core/profile_storage.c
    src/core/registration_cache.c
    src/core/session_manager.c
    src/core/settings.c
    src/core/vitarps5.c

    # VitaRPS5 Utils
    src/utils/diagnostics.c
    src/utils/error_strings.c
    src/utils/helpers.c
    src/utils/http_client.c
    src/utils/json_parser.c
    src/utils/logger.c
    src/utils/protocol_retry.c

    # VitaRPS5 Audio
    src/audio/audio_decoder.c

    # VitaRPS5 System
    src/system/vita_system_info.c

    # VitaRPS5 Discovery
    src/discovery/discovery.c
    src/discovery/ps5_discovery.c

    # VitaRPS5 Network
    src/network/connect_local.c
    src/network/control_plane.c
    src/network/network_manager.c
    src/network/network_stubs.c
    src/network/session_init.c
    src/network/session_keepalive.c
    src/network/takion.c
    src/network/wake.c

    # VitaRPS5 Console Bridge
    src/console/vitaki_bridge.c

    # VitaRPS5 Chiaki Integration
    src/chiaki/chiaki_base64_vitaki.c
    src/chiaki/chiaki_common.c
    src/chiaki/chiaki_controller.c
    src/chiaki/chiaki_ctrl_impl.c
    src/chiaki/chiaki_ecdh_vitaki.c
    src/chiaki/chiaki_gkcrypt_vitaki.c
    src/chiaki/chiaki_http_vitaki.c
    src/chiaki/chiaki_integration.c
    src/chiaki/chiaki_launchspec.c
    src/chiaki/chiaki_log.c
    src/chiaki/chiaki_missing_functions.c
    src/chiaki/chiaki_random.c
    src/chiaki/chiaki_receivers.c
    src/chiaki/chiaki_regist_vitaki.c
    src/chiaki/chiaki_rpcrypt_vitaki.c
    src/chiaki/chiaki_rudp.c
    src/chiaki/chiaki_session.c
    src/chiaki/chiaki_sock.c
    src/chiaki/chiaki_streamconnection_vitaki.c
    src/chiaki/chiaki_streamconnection.c
    src/chiaki/chiaki_target_utils.c
    src/chiaki/chiaki_thread_vita.c
    src/chiaki/chiaki_threading.c
    src/chiaki/pb_utils.c
    src/chiaki/vitaki_stoppipe.c
    src/chiaki/vitaki_thread.c
    src/chiaki/vitaki_time.c

    # VitaRPS5 UI modules
    src/ui/console_state_thread.c
    src/ui/ui_components.c
    src/ui/ui_controller.c
    src/ui/ui_core.c
    src/ui/ui_dashboard.c
    src/ui/ui_navigation.c
    src/ui/ui_profile.c
    src/ui/ui_psn_login.c
    src/ui/ui_registration.c
    src/ui/ui_settings.c
    src/ui/ui_streaming.c
    src/ui/vita2d_ui.c

    # Third party
    third_party/tomlc99/toml.c
    third_party/h264-bitstream/h264_nal.c
	third_party/h264-bitstream/h264_sei.c
	third_party/h264-bitstream/h264_stream.c
)


target_compile_definitions(${VITA_APP_NAME}.elf PUBLIC
    CHIAKI_VERSION="${CHIAKI_VERSION}"
)

target_include_directories(${VITA_APP_NAME}.elf PRIVATE
    include
    src
    third_party
    third_party/h264-bitstream/
)

target_link_libraries(${VITA_APP_NAME}.elf
  # TODO: Do we really need all of those?
  chiaki-lib
  vita2d
  ssl
  crypto
  opus
  curl

  freetype
  bz2
  png
  jpeg
  z
  m
  c
  -Wl,--whole-archive
  pthread
  -Wl,--no-whole-archive
  zip

  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  SceCtrl_stub
  ScePgf_stub
  ScePvf_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceRegistryMgr_stub

  SceMotion_stub
  SceTouch_stub
#   SceHttp_stub
  SceNet_stub
  SceNetCtl_stub
  ScePower_stub
  SceVideodec_stub
  SceCodecEngine_stub
  SceAudio_stub
  SceIme_stub
  SceSsl_stub
  ScePower_stub
)


if(CHIAKI_IS_VITA)
    vita_create_self(${VITA_APP_NAME}.self ${VITA_APP_NAME}.elf UNSAFE)

    vita_create_vpk(${VITA_APP_NAME}.vpk ${VITA_TITLEID} ${VITA_APP_NAME}.self
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        # Livearea stuff
        FILE res/icon0.png sce_sys/icon0.png
        FILE res/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
        FILE res/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
        FILE res/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        # Textures, other assets
        FILE res/assets/btn_add_active.png assets/btn_add_active.png
        FILE res/assets/btn_add.png assets/btn_add.png
        FILE res/assets/btn_discovery_active.png assets/btn_discovery_active.png
        FILE res/assets/btn_discovery_off_active.png assets/btn_discovery_off_active.png
        FILE res/assets/btn_discovery_off.png assets/btn_discovery_off.png
        FILE res/assets/btn_discovery.png assets/btn_discovery.png
        FILE res/assets/btn_register_active.png assets/btn_register_active.png
        FILE res/assets/btn_register.png assets/btn_register.png
        FILE res/assets/btn_settings_active.png assets/btn_settings_active.png
        FILE res/assets/btn_settings.png assets/btn_settings.png
        FILE res/assets/btn_messages_active.png assets/btn_messages_active.png
        FILE res/assets/btn_messages.png assets/btn_messages.png
        FILE res/assets/discovered_host.png assets/discovered_host.png
        FILE res/assets/header_logo.png assets/header_logo.png
        FILE res/assets/ps4_off.png assets/ps4_off.png
        FILE res/assets/ps4.png assets/ps4.png
        FILE res/assets/ps4_rest.png assets/ps4_rest.png
        FILE res/assets/ps5_off.png assets/ps5_off.png
        FILE res/assets/ps5.png assets/ps5.png
        FILE res/assets/ps5_rest.png assets/ps5_rest.png
        FILE res/assets/fonts/Roboto-Regular.ttf assets/fonts/Roboto-Regular.ttf
        FILE res/assets/fonts/RobotoMono-Regular.ttf assets/fonts/RobotoMono-Regular.ttf
    )
endif()
