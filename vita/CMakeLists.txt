include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Vitaki")
set(VITA_TITLEID  "CHIAKI001")
# TODO: Make it dynamic
set(VITA_VERSION "00.06")

add_executable(${VITA_APP_NAME}.elf
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
        # VitaRPS5 UI Assets
        FILE res/assets/vitarps5/user_provided/modern_assets/symbol_triangle.png assets/vitarps5/symbol_triangle.png
        FILE res/assets/vitarps5/user_provided/modern_assets/symbol_circle.png assets/vitarps5/symbol_circle.png
        FILE res/assets/vitarps5/user_provided/modern_assets/symbol_ex.png assets/vitarps5/symbol_ex.png
        FILE res/assets/vitarps5/user_provided/modern_assets/symbol_square.png assets/vitarps5/symbol_square.png
        FILE res/assets/vitarps5/user_provided/modern_assets/wave_top.png assets/vitarps5/wave_top.png
        FILE res/assets/vitarps5/user_provided/modern_assets/wave_bottom.png assets/vitarps5/wave_bottom.png
        FILE res/assets/vitarps5/user_provided/modern_assets/ellipse_green.png assets/vitarps5/ellipse_green.png
        FILE res/assets/vitarps5/user_provided/modern_assets/ellipse_yellow.png assets/vitarps5/ellipse_yellow.png
        FILE res/assets/vitarps5/user_provided/modern_assets/ellipse_red.png assets/vitarps5/ellipse_red.png
        FILE res/assets/vitarps5/user_provided/modern_assets/button_add_new.png assets/vitarps5/button_add_new.png
        FILE res/assets/vitarps5/user_provided/modern_assets/console_card.png assets/vitarps5/console_card.png
        # Navigation Icons
        FILE res/assets/vitarps5/user_provided/icons/settings_white.png assets/vitarps5/icon_settings.png
        FILE res/assets/vitarps5/user_provided/icons/PS5_Controller_White.png assets/vitarps5/icon_controller.png
        FILE res/assets/vitarps5/user_provided/icons/profile.png assets/vitarps5/icon_profile.png
        FILE res/assets/ps5.png assets/vitarps5/icon_play.png
        # Professional UI assets (gradient background, logo, controller diagrams)
        FILE assets/background.png assets/background.png
        FILE assets/Vita_RPS5_Logo.png assets/Vita_RPS5_Logo.png
        FILE assets/Vita_Front.png assets/Vita_Front.png
        FILE assets/Vita_Back.png assets/Vita_Back.png
        FILE assets/PS5_logo.png assets/PS5_logo.png
    )
endif()
