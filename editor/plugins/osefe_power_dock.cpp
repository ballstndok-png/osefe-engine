/**************************************************************************/
/*  osefe_power_dock.cpp                                                   */
/**************************************************************************/

#include "osefe_power_dock.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/math/random_number_generator.h"
#include "core/object/callable_mp.h"
#include "core/templates/local_vector.h"
#include "core/math/math_defs.h"
#include "editor/editor_node.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/separator.h"

void OsefePowerDockPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			dock = memnew(VBoxContainer);
			dock->set_name("OsefePower");
			dock->set_custom_minimum_size(Size2(0, 0));

			Label *header = memnew(Label);
			header->set_text(TTR("Osefe Power"));
			header->add_theme_font_size_override("font_size", 18);
			dock->add_child(header);

			Label *hint = memnew(Label);
			hint->set_text(TTR("One-click generators — everything is created inside your project."));
			hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			dock->add_child(hint);

			Button *sfx_btn = memnew(Button);
			sfx_btn->set_text(TTR("Generate SFX (WAV)"));
			sfx_btn->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
			sfx_btn->connect(SceneStringName(pressed), callable_mp(this, &OsefePowerDockPlugin::_generate_sfx));
			dock->add_child(sfx_btn);

			Button *level_btn = memnew(Button);
			level_btn->set_text(TTR("Generate 2D Level"));
			level_btn->connect(SceneStringName(pressed), callable_mp(this, &OsefePowerDockPlugin::_generate_level));
			dock->add_child(level_btn);

			Button *char_btn = memnew(Button);
			char_btn->set_text(TTR("Generate Character"));
			char_btn->connect(SceneStringName(pressed), callable_mp(this, &OsefePowerDockPlugin::_generate_character));
			dock->add_child(char_btn);

			dock->add_child(memnew(HSeparator));

			Button *turbo_btn = memnew(Button);
			turbo_btn->set_text(TTR("Turbo ⚡ (Performance Boost)"));
			turbo_btn->set_tooltip_text(TTR("Enables FSR 2 upscaling, occlusion culling and leaner quality settings for this project, then saves them to project.godot."));
			turbo_btn->connect(SceneStringName(pressed), callable_mp(this, &OsefePowerDockPlugin::_apply_turbo));
			dock->add_child(turbo_btn);

			dock->add_spacer();

			add_control_to_dock(DOCK_SLOT_RIGHT_UR, dock);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (dock) {
				remove_control_from_docks(dock);
				memdelete(dock);
				dock = nullptr;
			}
		} break;
	}
}

String OsefePowerDockPlugin::get_plugin_name() const {
	return "Osefe Power";
}

static Error _osefe_write_text(const String &p_path, const String &p_text) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		return ERR_CANT_CREATE;
	}
	f->store_string(p_text);
	return OK;
}

void OsefePowerDockPlugin::_generate_sfx() {
	// A short chiptune-style "pickup" sound: rising sine sweep with decay.
	const int rate = 44100;
	const int samples = (int)(rate * 0.35);
	LocalVector<uint8_t> data;
	data.resize(44 + samples * 2);

	auto put16 = [&](int ofs, uint16_t v) {
		data[ofs] = v & 0xFF;
		data[ofs + 1] = (v >> 8) & 0xFF;
	};
	auto put32 = [&](int ofs, uint32_t v) {
		data[ofs] = v & 0xFF;
		data[ofs + 1] = (v >> 8) & 0xFF;
		data[ofs + 2] = (v >> 16) & 0xFF;
		data[ofs + 3] = (v >> 24) & 0xFF;
	};

	// RIFF header.
	data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
	put32(4, 36 + samples * 2);
	data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
	data[12] = 'f'; data[13] = 'm'; data[14] = 't'; data[15] = ' ';
	put32(16, 16);
	put16(20, 1); // PCM
	put16(22, 1); // mono
	put32(24, rate);
	put32(28, rate * 2);
	put16(32, 2);
	put16(34, 16);
	data[36] = 'd'; data[37] = 'a'; data[38] = 't'; data[39] = 'a';
	put32(40, samples * 2);

	for (int i = 0; i < samples; i++) {
		float t = (float)i / rate;
		float freq = 440.0f + 460.0f * (t / 0.35f);
		float env = 1.0f - t / 0.35f;
		float s = Math::sin(Math::TAU * freq * t) * env * 0.6f;
		int16_t v = (int16_t)(CLAMP(s, -1.0f, 1.0f) * 32767.0f);
		data[44 + i * 2] = v & 0xFF;
		data[44 + i * 2 + 1] = (v >> 8) & 0xFF;
	}

	Ref<FileAccess> f = FileAccess::open("res://osefe_sfx_pickup.wav", FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_buffer(data.ptr(), data.size());
		EditorNode::get_singleton()->show_warning(TTR("SFX saved to res://osefe_sfx_pickup.wav — drag it into any AudioStreamPlayer!"));
	}
}

void OsefePowerDockPlugin::_generate_level() {
	// A small platformer level with random floating platforms.
	RandomNumberGenerator rng;
	rng.randomize();

	String platforms;
	int count = 6 + rng.randi_range(0, 2);
	for (int i = 0; i < count; i++) {
		float x = 200.0f + rng.randf_range(0.0f, 900.0f);
		float y = 150.0f + rng.randf_range(0.0f, 250.0f);
		platforms += vformat(
				"[node name=\"Platform%d\" type=\"StaticBody2D\" parent=\".\"]\n"
				"position = Vector2(%.0f, %.0f)\n"
				"\n"
				"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Platform%d\"]\n"
				"shape = SubResource(\"RectangleShape2D_p\")\n"
				"\n"
				"[node name=\"Visual\" type=\"ColorRect\" parent=\"Platform%d\"]\n"
				"offset_left = -100.0\n"
				"offset_top = -10.0\n"
				"offset_right = 100.0\n"
				"offset_bottom = 10.0\n"
				"color = Color(0.55, 0.4, 1, 1)\n"
				"\n",
				i, x, y, i, i);
	}

	String scene_text =
			"[gd_scene load_steps=2 format=3]\n"
			"\n"
			"[sub_resource type=\"RectangleShape2D\" id=\"RectangleShape2D_p\"]\n"
			"size = Vector2(200, 20)\n"
			"\n"
			"[node name=\"OsefeLevel\" type=\"Node2D\"]\n"
			"\n"
			"[node name=\"Ground\" type=\"StaticBody2D\" parent=\".\"]\n"
			"position = Vector2(540, 560)\n"
			"\n"
			"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Ground\"]\n"
			"shape = SubResource(\"RectangleShape2D_p\")\n"
			"\n"
			"[node name=\"Visual\" type=\"ColorRect\" parent=\"Ground\"]\n"
			"offset_left = -540.0\n"
			"offset_top = -10.0\n"
			"offset_right = 540.0\n"
			"offset_bottom = 10.0\n"
			"color = Color(0.24, 0.2, 0.36, 1)\n"
			"\n" +
			platforms;

	if (_osefe_write_text("res://osefe_level.tscn", scene_text) == OK) {
		EditorNode::get_singleton()->load_scene("res://osefe_level.tscn");
	}
}

void OsefePowerDockPlugin::_generate_character() {
	const String script_text =
			"extends CharacterBody2D\n"
			"\n"
			"const SPEED := 400.0\n"
			"const JUMP_VELOCITY := -700.0\n"
			"const GRAVITY := 1600.0\n"
			"\n"
			"func _physics_process(delta: float) -> void:\n"
			"\tif not is_on_floor():\n"
			"\t\tvelocity.y += GRAVITY * delta\n"
			"\tif Input.is_action_just_pressed(\"ui_accept\") and is_on_floor():\n"
			"\t\tvelocity.y = JUMP_VELOCITY\n"
			"\tvar direction := Input.get_axis(\"ui_left\", \"ui_right\")\n"
			"\tif direction:\n"
			"\t\tvelocity.x = direction * SPEED\n"
			"\telse:\n"
			"\t\tvelocity.x = move_toward(velocity.x, 0.0, SPEED)\n"
			"\tmove_and_slide()\n";

	const String scene_text =
			"[gd_scene load_steps=3 format=3]\n"
			"\n"
			"[ext_resource type=\"Script\" path=\"res://osefe_character.gd\" id=\"1_script\"]\n"
			"\n"
			"[sub_resource type=\"RectangleShape2D\" id=\"RectangleShape2D_1\"]\n"
			"size = Vector2(40, 60)\n"
			"\n"
			"[node name=\"OsefeCharacter\" type=\"CharacterBody2D\"]\n"
			"script = ExtResource(\"1_script\")\n"
			"\n"
			"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\".\"]\n"
			"shape = SubResource(\"RectangleShape2D_1\")\n"
			"\n"
			"[node name=\"Visual\" type=\"ColorRect\" parent=\".\"]\n"
			"offset_left = -20.0\n"
			"offset_top = -30.0\n"
			"offset_right = 20.0\n"
			"offset_bottom = 30.0\n"
			"color = Color(0.9, 0.85, 1, 1)\n";

	bool script_ok = _osefe_write_text("res://osefe_character.gd", script_text) == OK;
	bool scene_ok = _osefe_write_text("res://osefe_character.tscn", scene_text) == OK;
	if (script_ok && scene_ok) {
		EditorNode::get_singleton()->load_scene("res://osefe_character.tscn");
	}
}

void OsefePowerDockPlugin::_apply_turbo() {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	ps->set("rendering/scaling_3d/mode", 2); // FSR 2
	ps->set("rendering/scaling_3d/scale", 0.9);
	ps->set("rendering/occlusion_culling/use_occlusion_culling", true);
	ps->set("rendering/anti_aliasing/quality/msaa_3d", 0);
	ps->set("rendering/lights_and_shadows/positional_shadow/soft_shadow_filter_quality", 1);
	ps->save();

	EditorNode::get_singleton()->show_warning(
			TTR("Turbo ⚡ applied and saved to project.godot:\n• FSR 2 upscaling at 0.9 scale\n• Occlusion culling ON\n• MSAA off, leaner shadows\n\nYour game should run noticeably faster."));
}

OsefePowerDockPlugin::OsefePowerDockPlugin() {
}
