# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# PlayerDB creates Restart.fl from NewPlayer.fl beside the executable: it is tracked in bin\ and
# copied into the other output directories by copy_dev_ini.py (the dev_ini target).

fl_add_module(freelancer EXE RETAIL PCH EXPORTS
  SOURCES
    Client/state.cpp
    Client/cmn/voice_profiles.cpp
    Client/cmn/sound_config.cpp
    Client/nn/nn_preferences.cpp
    Client/interface/wincontrol_base.cpp
    Client/interface/win3d.cpp
    Client/interface/text_win.cpp
    Client/interface/type_select.cpp
    Client/interface/page_options.cpp
    Client/interface/page_account_select.cpp
    Client/interface/win_object.cpp
    Client/interface/list_box.cpp
    Client/interface/rich_text_win.cpp
    Client/interface/ui_page_desc.cpp
    Client/interface/ui_page.cpp
    Client/interface/ui_rich_text_win.cpp
    Client/interface/win_factory.cpp
    Client/interface/ui_static_text.cpp
    Client/interface/ui_list_box.cpp
    Client/interface/ui_scroll.cpp
    Client/nn/nn_button_control.cpp
    Client/nn/nn_static_control.cpp
    Client/nn/nn_montage_draw.cpp
    Client/nn/nn_text_control.cpp
    Client/interface/wire_controls.cpp
    Client/nn/nn_prompt.cpp
    Client/nn/nn_modal.cpp
    Client/nn/nn_main_button.cpp
    Client/gf/nav_bar.cpp
    Client/interface/infocard_map.cpp
    Client/nn/knowledge_map.cpp
    Client/nn/nn_messaging.cpp
    Client/nn/nn_mission_message.cpp
    Client/gf/character_info.cpp
    Client/nn/nn_exit.cpp
    Client/nn/nn_help.cpp
    Client/nn/nn_player_info.cpp
    Client/nn/nn_offer_dialog.cpp
    Client/nn/nn_pause.cpp
    Client/nn/nn_confirm.cpp
    Client/nn/nn_load_save.cpp
    Client/nn/nn_network_operation.cpp
    Client/nn/nn_error.cpp
    Client/nn/nn_blocking_dialog.cpp
    Client/interface/wincontrol_children.cpp
    Client/nn/nn_ui_pulse.cpp
    Client/nn/nn_tooltip.cpp
    Client/nn/nn_rollover.cpp
    Client/nn/nn_group_control.cpp
    Client/interface/menu_mp_session.cpp
    Client/cmn/font_string.cpp
    Client/cmn/font_imp.cpp
    Client/cmn/font_atlas.cpp
    Client/interface/menu_enter_state.cpp
    Client/interface/ui_text_msg_button.cpp
    Client/interface/ui_text_edit.cpp
    Client/interface/ui_win_deform.cpp
    Client/interface/ui_ship_view_control.cpp
    Client/interface/ui_win_object.cpp
    Client/interface/ui_table.cpp
    Client/interface/ui_fast_table.cpp
    Client/nn/mission_briefing.cpp
    Client/frame_tick.cpp
    Client/tick_profile.cpp
    Client/connect_handshake.cpp
    Client/connect_lists.cpp
    Client/login_mgr.cpp
    Client/space/cockpit_load.cpp
    Client/space/player_behavior.cpp
    Client/space/player_spawn.cpp
    Client/space/objdb.cpp
    Client/space/gameobj.cpp
    Client/space/debris.cpp
    Client/space/explosion.cpp
    Client/space/land_action.cpp
    Client/interface/character_select_equip.cpp
    Client/space/equip_fx.cpp
    Client/space/fuse.cpp
    Client/space/tradelane_fx.cpp
    Client/space/effect_base.cpp
    Client/cmn/effect_vertex_batch.cpp
    Client/space/beam_effect.cpp
    Client/space/cli_projectile_fx.cpp
    Client/space/cli_tractor_beam.cpp
    Client/cmn/fl_fx_mesh.cpp
    Client/space/particle_effect.cpp
    Client/cmn/alchemy_float_forward.cpp
    Client/space/effect_type_info.cpp
    Client/space/effect_type_list.cpp
    Client/space/camera/ship_camera_params.cpp
    Client/space/camera/space_camera.cpp
    Client/space/camera/space_camera_third.cpp
    Client/space/camera/space_camera_game.cpp
    Client/space/camera/space_camera_rear.cpp
    Client/space/camera/space_camera_turret.cpp
    Client/space/camera/space_camera_first.cpp
    Client/space/camera/space_camera_script.cpp
    Client/space/space_letterbox.cpp
    Client/cmn/fade_to_black.cpp
    Client/space/camera/space_camera_static.cpp
    Client/space/camera/space_camera_docking.cpp
    Client/space/camera/space_camera_death.cpp
    Client/cmn/win3d_immediate.cpp
    Client/space/cli_asteroid.cpp
    Client/space/cli_asteroid_dynamic.cpp
    Client/space/cli_asteroid_med.cpp
    Client/space/asteroid_billboard.cpp
    Client/space/cli_nebula.cpp
    Client/space/texture_panels.cpp
    Client/space/star_sphere.cpp
    Client/space/star_sys.cpp
    Client/cmn/part_cache.cpp
    Client/space/space_dust.cpp
    Client/space/space_scene_draw.cpp
    Client/space/space_scene_obj_draw.cpp
    Client/cmn/render_ex.cpp
    Client/space/nebula_volume.cpp
    Client/space/space_scene_load.cpp
    Client/space/space_solar_world.cpp
    Client/cmn/scene_render_registry.cpp
    Client/space/space_state.cpp
    Client/interface/win3d_render.cpp
    Client/interface/win3d_engine_submit.cpp
    Client/interface/win3d_renderables.cpp
    Client/intro_connect.cpp
    Client/interface/image.cpp
    Client/interface/loading_screen.cpp
    Client/cmn/pixel_format.cpp
    Client/gf/menu_universe_base.cpp
    Client/gf/room_control.cpp
    Client/gf/room_control_hotspot.cpp
    Client/gf/anim_script_set.cpp
    Client/space/flash_light.cpp
    Client/gf/gameflow.cpp
    Client/gf/base_walk.cpp
    Client/gf/gf_hotspot.cpp
    Client/gf/object_prop.cpp
    Client/gf/room_camera.cpp
    Client/gf/gf_script.cpp
    Client/gf/gf_script_object.cpp
    Client/gf/gf_thorn_component.cpp
    Client/gf/gf_thorn_compound.cpp
    Client/gf/gf_thorn_deformable.cpp
    Client/gf/gf_character_object.cpp
    Client/gf/gf_script_spawn.cpp
    Client/gf/gf_thorn_camera.cpp
    Client/gf/gf_thorn_light.cpp
    Client/gf/gf_thorn_marker.cpp
    Client/gf/gf_thorn_psys.cpp
    Client/gf/gf_thorn_scene.cpp
    Client/gf/gf_thorn_sound.cpp
    Client/gf/gf_object_ship.cpp
    Client/cmn/lights_camera.cpp
    Client/cmn/menu_sound_arch.cpp
    Client/cmn/engine_light.cpp
    Client/cmn/graph_library.cpp
    Client/cmn/misc_util.cpp
    Client/cmn/texture_shapes.cpp
    Client/space/hud/hud_element_list.cpp
    Client/space/hud/hud_command_dispatch.cpp
    Client/space/hud/hud_comm.cpp
    Client/space/hud/hud_targetable_objects.cpp
    Client/interface/button_mover.cpp
    Client/space/hud/hud_target.cpp
    Client/space/hud/hud_target_panel.cpp
    Client/space/hud/hud_status_core.cpp
    Client/space/hud/hud_weapon_groups.cpp
    Client/space/hud/hud_cruise_progress.cpp
    Client/space/hud/hud_contact_list.cpp
    Client/space/hud/hud_damage_indicator.cpp
    Client/space/hud/hud_maneuvers.cpp
    Client/space/hud/hud_waypoint.cpp
    Client/space/hud/hud_widescreen.cpp
    Client/space/jump_effect.cpp
    Client/nn/best_path.cpp
    Client/cmn/cliobj.cpp
    # Static-init order (retail XC table): the material cache (Client/cmn/cliobj.cpp, slot 0x0060f0bc)
    # is built before the VisEffect map (Client/cmn/vis_effect_arch.cpp, slot 0x0060f19c), so ~FVisEffectArch
    # (FUN_0042edb0) still finds it at exit. MSVC runs .CRT$XCU in link order, which is this
    # order: keep Client/cmn/vis_effect_arch.cpp after Client/cmn/cliobj.cpp.
    Client/cmn/vis_effect_arch.cpp
    Client/space/mesh_load.cpp
    Client/cmn/engine_character.cpp
    Client/space/hud/hud_button.cpp
    Client/cmn/input_manager.cpp
    Client/interface/ime_focus.cpp
    Client/interface/fl_ime_input_box.cpp
    Client/nn/chat_text_input.cpp
    Client/nn/chat_control.cpp
    Client/perf_options.cpp
    Client/interface/menu_ui_draw.cpp
    Client/interface/menu_ui_widgets.cpp
    Client/nn/nn_repair.cpp
    Client/nn/nn_story_star.cpp
    Client/nn/nn_ship_trader.cpp
    Client/nn/nn_trade.cpp
    Client/interface/cargo_bar_control.cpp
    Client/space/equip_visual_set.cpp
    Client/interface/scroll_control.cpp
    Client/nn/trade_equip_list.cpp
    Client/interface/ship_view_control.cpp
    Client/nn/nn_inventory.cpp
    Client/nn/nn_dealer.cpp
    Client/nn/commodity_by_base.cpp
    Client/gf/base_market.cpp
    Client/cmn/swapped_material.cpp
    Client/nn/nn_mission_vendor.cpp
    Client/nn/nn_news.cpp
    Client/nn/nn_navmap.cpp
    Client/nn/nn_navmap_panels.cpp
    Client/nn/nn_navmap_update.cpp
    Client/nn/nn_navmap_build.cpp
    Client/nn/nn_navmap_view.cpp
    Client/nn/nn_navmap_draw.cpp
    Client/nn/nn_navmap_input.cpp
    Client/space/mission_created_solars.cpp
    Client/nn/nn_scanner.cpp
    Client/nn/nn_text_scratch.cpp
    Client/main_command_handler.cpp
    Client/interface/menu_keymap.cpp
    Client/cmn/screen_quad_state.cpp
    Client/connect_flow.cpp
    Client/exports.cpp
    Client/iclient.cpp
    Client/space/iclient_space.cpp
    Client/main.cpp
    Client/interface/window.cpp
    Client/message_pump.cpp
    Client/cmn/engine_init.cpp
    Client/interface/menu_ui_init.cpp
    Client/dataconfig_progress.cpp
    Client/interface/menu_fonts_load.cpp
    Client/ids_strings.cpp
    Client/ids_rdl.cpp
    Client/nn/dealer_equip_stats.cpp
    Client/nn/chat_init.cpp
    Client/game_options.cpp
    Client/space/gate_tunnel.cpp
    Client/timing.cpp
    Client/server_connect.cpp
    Client/server_filter.cpp
    Client/reg_key_stack.cpp
    Client/interface/page_server_select.cpp
    Client/server_filter_dialog.cpp
    Client/interface/intro_page_util.cpp
    Client/interface/page_character_create.cpp
    Client/interface/page_character_select.cpp
    Client/interface/page_character_select_single.cpp
    Client/startup_config.cpp
    resources/freelancer.rc
)

# FREELANCER_EXPORTS selects the declarations freelancer.exe exports itself (retail FreeLancer.exe
# carries a 69-name export table, no .def): see the export macros in src/include/rpc_iclient.h and
# src/include/common_exports.h.
fl_defines(freelancer FREELANCER_EXPORTS)
fl_rc(freelancer INCLUDES resources)

# The port-only switches (cmake/Freelancer.cmake): only the client reads them.
if(FreelancerBetaLabel)
  fl_defines(freelancer FREELANCER_BETA_LABEL=1)
endif()
if(FreelancerWidescreenSupport)
  fl_defines(freelancer FREELANCER_WIDESCREEN_SUPPORT)
endif()
if(FreelancerRp9)
  fl_defines(freelancer FREELANCER_RP9)
endif()
if(FreelancerWalkMode)
  fl_defines(freelancer FREELANCER_WALK_MODE)
endif()

fl_item(Client/cmn/alchemy_float_forward.cpp
  BasicRuntimeChecks Default Optimization MaxSpeed DebugInformationFormat ProgramDatabase
  SupportJustMyCode false EnableEnhancedInstructionSet StreamingSIMDExtensions2)

if(FL_WIN32)
  set(_libdirs ../bin ${PROJECT_SOURCE_DIR}/bin)
else()
  set(_libdirs)
endif()
fl_link(freelancer
  LIBS dacom.lib DALib.lib Common.lib imeui.lib ole32.lib gdi32.lib user32.lib advapi32.lib
       shell32.lib winmm.lib
  REFERENCES dacom DALib Common imeui
  LIBDIRS ${_libdirs})

# DebugASan: the AddressSanitizer runtime DLLs next to the exe.
fl_copy_asan_runtime(freelancer)
