/**************************************************************************/
/*  project_dialog.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "project_dialog.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/zip_io.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_icons.h"
#include "editor/themes/editor_scale.h"
#include "editor/version_control/editor_vcs_interface.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/link_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"

void ProjectDialog::_set_message(const String &p_msg, MessageType p_type, InputType p_input_type) {
	msg->set_text(p_msg);

	if (p_type == MESSAGE_ERROR) {
		invalid_state_flags.set_flag(InvalidStateFlag::INVALID_STATE_FLAG_PATH_INPUT);
	} else {
		invalid_state_flags.clear_flag(InvalidStateFlag::INVALID_STATE_FLAG_PATH_INPUT);
	}

	Ref<Texture2D> new_icon;
	switch (p_type) {
		case MESSAGE_ERROR: {
			msg->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			new_icon = get_editor_theme_icon(SNAME("StatusError"));
		} break;
		case MESSAGE_WARNING: {
			msg->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
			new_icon = get_editor_theme_icon(SNAME("StatusWarning"));
		} break;
		case MESSAGE_SUCCESS: {
			msg->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("success_color"), EditorStringName(Editor)));
			new_icon = get_editor_theme_icon(SNAME("StatusSuccess"));
		} break;
	}

	if (p_input_type == PROJECT_PATH) {
		project_status_rect->set_texture(new_icon);
	} else if (p_input_type == INSTALL_PATH) {
		install_status_rect->set_texture(new_icon);
	}

	_update_ok_button();
}

void ProjectDialog::_update_ok_button() {
	get_ok_button()->set_disabled(!invalid_state_flags.is_empty());
}

static bool is_zip_file(Ref<DirAccess> p_d, const String &p_path) {
	return p_path.get_extension() == "zip" && p_d->file_exists(p_path);
}

void ProjectDialog::_validate_path() {
	_set_message("", MESSAGE_SUCCESS, PROJECT_PATH);
	_set_message("", MESSAGE_SUCCESS, INSTALL_PATH);

	if (project_name->get_text().strip_edges().is_empty()) {
		_set_message(TTRC("It would be a good idea to name your project."), MESSAGE_ERROR);
		return;
	}

	Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	String path = project_path->get_text().simplify_path();

	String target_path = path;
	InputType target_path_input_type = PROJECT_PATH;

	if (mode == MODE_IMPORT) {
		if (path.get_file().strip_edges() == "project.godot") {
			path = path.get_base_dir();
			project_path->set_text(path);
		}

		if (is_zip_file(d, path)) {
			zip_path = path;
		} else if (is_zip_file(d, path.strip_edges())) {
			zip_path = path.strip_edges();
		} else {
			zip_path = "";
		}

		if (!zip_path.is_empty()) {
			target_path = install_path->get_text().simplify_path();
			target_path_input_type = INSTALL_PATH;

			create_dir->show();
			install_path_container->show();

			Ref<FileAccess> io_fa;
			zlib_filefunc_def io = zipio_create_io(&io_fa);

			unzFile pkg = unzOpen2(zip_path.utf8().get_data(), &io);
			if (!pkg) {
				_set_message(TTRC("Invalid \".zip\" project file; it is not in ZIP format."), MESSAGE_ERROR);
				unzClose(pkg);
				return;
			}

			int ret = unzGoToFirstFile(pkg);
			while (ret == UNZ_OK) {
				unz_file_info info;
				char fname[16384];
				ret = unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
				ERR_FAIL_COND_MSG(ret != UNZ_OK, "Failed to get current file info.");

				String name = String::utf8(fname);

				// Skip the __MACOSX directory created by macOS's built-in file zipper.
				if (name.begins_with("__MACOSX")) {
					ret = unzGoToNextFile(pkg);
					continue;
				}

				if (name.get_file() == "project.godot") {
					break; // ret == UNZ_OK.
				}

				ret = unzGoToNextFile(pkg);
			}

			if (ret == UNZ_END_OF_LIST_OF_FILE) {
				_set_message(TTRC("Invalid \".zip\" project file; it doesn't contain a \"project.godot\" file."), MESSAGE_ERROR);
				unzClose(pkg);
				return;
			}

			unzClose(pkg);
		} else if (d->dir_exists(path) && d->file_exists(path.path_join("project.godot"))) {
			zip_path = "";

			create_dir->hide();
			install_path_container->hide();

			_set_message(TTRC("Valid project found at path."), MESSAGE_SUCCESS);
		} else {
			create_dir->hide();
			install_path_container->hide();

			_set_message(TTRC("Please choose a \"project.godot\", a directory with one, or a \".zip\" file."), MESSAGE_ERROR);
			return;
		}
	}

	if (target_path.is_relative_path()) {
		_set_message(TTRC("The path specified is invalid."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	if (target_path.get_file() != OS::get_singleton()->get_safe_dir_name(target_path.get_file())) {
		_set_message(TTRC("The directory name specified contains invalid characters or trailing whitespace."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	String working_dir = d->get_current_dir();
	String executable_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	if (target_path == working_dir || target_path == executable_dir) {
		_set_message(TTRC("Creating a project at the engine's working directory or executable directory is not allowed, as it would prevent the project manager from starting."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	// TODO: The following 5 lines could be simplified if OS.get_user_home_dir() or SYSTEM_DIR_HOME is implemented. See: https://github.com/godotengine/godot-proposals/issues/4851.
#ifdef WINDOWS_ENABLED
	String home_dir = OS::get_singleton()->get_environment("USERPROFILE");
#else
	String home_dir = OS::get_singleton()->get_environment("HOME");
#endif
	String documents_dir = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS);
	if (target_path == home_dir || target_path == documents_dir) {
		_set_message(TTRC("You cannot save a project at the selected path. Please create a subfolder or choose a new path."), MESSAGE_ERROR, target_path_input_type);
		return;
	}

	is_folder_empty = true;
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE || (mode == MODE_IMPORT && target_path_input_type == InputType::INSTALL_PATH)) {
		if (create_dir->is_pressed()) {
			if (!d->dir_exists(target_path.get_base_dir())) {
				_set_message(TTRC("The parent directory of the path specified doesn't exist."), MESSAGE_ERROR, target_path_input_type);
				return;
			}

			if (d->dir_exists(target_path)) {
				// The path is not necessarily empty here, but we will update the message later if it isn't.
				_set_message(TTRC("The project folder already exists and is empty."), MESSAGE_SUCCESS, target_path_input_type);
			} else {
				_set_message(TTRC("The project folder will be automatically created."), MESSAGE_SUCCESS, target_path_input_type);
			}
		} else {
			if (!d->dir_exists(target_path)) {
				_set_message(TTRC("The path specified doesn't exist."), MESSAGE_ERROR, target_path_input_type);
				return;
			}

			// The path is not necessarily empty here, but we will update the message later if it isn't.
			_set_message(TTRC("The project folder exists and is empty."), MESSAGE_SUCCESS, target_path_input_type);
		}

		// Check if the directory is empty. Not an error, but we want to warn the user.
		if (d->change_dir(target_path) == OK) {
			d->list_dir_begin();
			String n = d->get_next();
			while (!n.is_empty()) {
				if (n[0] != '.') {
					// Allow `.`, `..` (reserved current/parent folder names)
					// and hidden files/folders to be present.
					// For instance, this lets users initialize a Git repository
					// and still be able to create a project in the directory afterwards.
					is_folder_empty = false;
					break;
				}
				n = d->get_next();
			}
			d->list_dir_end();

			if (!is_folder_empty) {
				_set_message(TTRC("The selected path is not empty. Choosing an empty folder is highly recommended."), MESSAGE_WARNING, target_path_input_type);
			}
		}
	}

	// Check if the target path is a subdirectory of original when duplicating
	if (mode == MODE_DUPLICATE) {
		String base_path = original_project_path;
		String duplicate_target = target_path;

		// Ensure the paths end with a slash
		if (!base_path.ends_with("/")) {
			base_path += "/";
		}

		if (!duplicate_target.ends_with("/")) {
			duplicate_target += "/";
		}

		bool is_subdirectory_or_equal;

		if (d->is_case_sensitive(base_path) || d->is_case_sensitive(duplicate_target)) {
			is_subdirectory_or_equal = duplicate_target.begins_with(base_path);
		} else {
			base_path = base_path.to_lower();
			String target_lower = duplicate_target.to_lower();
			is_subdirectory_or_equal = target_lower.begins_with(base_path);
		}

		if (is_subdirectory_or_equal) {
			_set_message(TTRC("Cannot duplicate a project into itself."), MESSAGE_ERROR, target_path_input_type);
		}
	}
}

String ProjectDialog::_get_target_path() {
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		return project_path->get_text();
	} else if (mode == MODE_IMPORT) {
		return install_path->get_text();
	} else {
		ERR_FAIL_V("");
	}
}
void ProjectDialog::_set_target_path(const String &p_text) {
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		project_path->set_text(p_text);
	} else if (mode == MODE_IMPORT) {
		install_path->set_text(p_text);
	} else {
		ERR_FAIL();
	}
}

void ProjectDialog::_update_target_auto_dir() {
	String new_auto_dir;
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		new_auto_dir = project_name->get_text();
	} else if (mode == MODE_IMPORT) {
		new_auto_dir = project_path->get_text().get_file().get_basename();
	}
	int naming_convention = (int)EDITOR_GET("project_manager/directory_naming_convention");
	switch (naming_convention) {
		case 0: // No Convention
			break;
		case 1: // kebab-case
			new_auto_dir = new_auto_dir.to_kebab_case();
			break;
		case 2: // snake_case
			new_auto_dir = new_auto_dir.to_snake_case();
			break;
		case 3: // camelCase
			new_auto_dir = new_auto_dir.to_camel_case();
			break;
		case 4: // PascalCase
			new_auto_dir = new_auto_dir.to_pascal_case();
			break;
		case 5: // Title Case
			new_auto_dir = new_auto_dir.capitalize();
			break;
		default:
			ERR_FAIL_MSG("Invalid directory naming convention.");
			break;
	}
	new_auto_dir = OS::get_singleton()->get_safe_dir_name(new_auto_dir);

	if (create_dir->is_pressed()) {
		String target_path = _get_target_path();

		if (target_path.get_file() == auto_dir) {
			// Update target dir name to new project name / ZIP name.
			target_path = target_path.get_base_dir().path_join(new_auto_dir);
		}

		_set_target_path(target_path);
	}

	auto_dir = new_auto_dir;
}

void ProjectDialog::_create_dir_toggled(bool p_pressed) {
	String target_path = _get_target_path();

	if (create_dir->is_pressed()) {
		// (Re-)append target dir name.
		if (last_custom_target_dir.is_empty()) {
			target_path = target_path.path_join(auto_dir);
		} else {
			target_path = target_path.path_join(last_custom_target_dir);
		}
	} else {
		// Strip any trailing slash.
		target_path = target_path.rstrip("/\\");
		// Save and remove target dir name.
		if (target_path.get_file() == auto_dir) {
			last_custom_target_dir = "";
		} else {
			last_custom_target_dir = target_path.get_file();
		}
		target_path = target_path.get_base_dir();
	}

	_set_target_path(target_path);
	_validate_path();
}

void ProjectDialog::_project_name_changed() {
	if (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		_update_target_auto_dir();
	}

	_validate_path();
}

void ProjectDialog::_project_path_changed() {
	if (mode == MODE_IMPORT) {
		_update_target_auto_dir();
	}

	_validate_path();
}

void ProjectDialog::_install_path_changed() {
	_validate_path();
}

void ProjectDialog::_browse_project_path() {
	String path = project_path->get_text();
	if (path.is_relative_path()) {
		path = EDITOR_GET("filesystem/directories/default_project_path");
	}
	if (mode == MODE_IMPORT && install_path->is_visible_in_tree()) {
		// Select last ZIP file.
		fdialog_project->set_current_path(path);
	} else if ((mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE) && create_dir->is_pressed()) {
		// Select parent directory of project path.
		fdialog_project->set_current_dir(path.get_base_dir());
	} else {
		// Select project path.
		fdialog_project->set_current_dir(path);
	}

	if (mode == MODE_IMPORT) {
		fdialog_project->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_ANY);
		fdialog_project->clear_filters();
		fdialog_project->add_filter("project.godot", vformat("%s %s", GODOT_VERSION_NAME, TTR("Project")));
		fdialog_project->add_filter("*.zip", TTR("ZIP File"));
	} else {
		fdialog_project->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	}

	hide();
	fdialog_project->popup_file_dialog();
}

void ProjectDialog::_browse_install_path() {
	ERR_FAIL_COND_MSG(mode != MODE_IMPORT, "Install path is only used for MODE_IMPORT.");

	String path = install_path->get_text();
	if (path.is_relative_path() || !DirAccess::dir_exists_absolute(path)) {
		path = EDITOR_GET("filesystem/directories/default_project_path");
	}
	if (create_dir->is_pressed()) {
		// Select parent directory of install path.
		fdialog_install->set_current_dir(path.get_base_dir());
	} else {
		// Select install path.
		fdialog_install->set_current_dir(path);
	}

	fdialog_install->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	fdialog_install->popup_file_dialog();
}

void ProjectDialog::_project_path_selected(const String &p_path) {
	show_dialog(false);

	if (create_dir->is_pressed() && (mode == MODE_NEW || mode == MODE_INSTALL || mode == MODE_DUPLICATE)) {
		// Replace parent directory, but keep target dir name.
		project_path->set_text(p_path.path_join(project_path->get_text().get_file()));
	} else {
		project_path->set_text(p_path);
	}

	_project_path_changed();

	if (install_path->is_visible_in_tree()) {
		// ZIP is selected; focus install path.
		install_path->grab_focus();
	} else {
		get_ok_button()->grab_focus();
	}
}

void ProjectDialog::_install_path_selected(const String &p_path) {
	ERR_FAIL_COND_MSG(mode != MODE_IMPORT, "Install path is only used for MODE_IMPORT.");

	if (create_dir->is_pressed()) {
		// Replace parent directory, but keep target dir name.
		install_path->set_text(p_path.path_join(install_path->get_text().get_file()));
	} else {
		install_path->set_text(p_path);
	}

	_install_path_changed();

	get_ok_button()->grab_focus();
}

void ProjectDialog::_reset_name() {
	project_name->set_text(TTR("New Game Project"));
}

void ProjectDialog::_starter_confirmation_confirmed() {
	starter_confirmed = true;
	ok_pressed();
}

void ProjectDialog::_starter_confirmation_declined() {
	starter_declined = true;
	ok_pressed();
}

void ProjectDialog::_project_type_selected() {
	ERR_FAIL_NULL(project_type_button_group->get_pressed_button());

	// Reset any previous decision when the user picks a different type.
	starter_confirmed = false;
	starter_declined = false;

	String project_type = project_type_button_group->get_pressed_button()->get_meta(SNAME("project_type"));

	if (project_type == "2d") {
		project_type_info->set_text(
				String::utf8("•  ") + TTR("Starter scene with a 2D root node.") +
				String::utf8("\n•  ") + TTR("Ready-to-run 2D game out of the box."));
	} else if (project_type == "3d") {
		project_type_info->set_text(
				String::utf8("•  ") + TTR("Starter scene with camera + lighting.") +
				String::utf8("\n•  ") + TTR("Ready-to-run 3D game out of the box."));
	} else if (project_type == "platformer") {
		project_type_info->set_text(
				String::utf8("•  ") + TTR("Complete playable platformer: running + jumping player, platforms.") +
				String::utf8("\n•  ") + TTR("Arrow keys to move, Space to jump. Press F5 and play!"));
	} else if (project_type == "topdown") {
		project_type_info->set_text(
				String::utf8("•  ") + TTR("Complete playable top-down game: 8-direction player, coins to collect.") +
				String::utf8("\n•  ") + TTR("Arrow keys to move. Press F5 and play!"));
	} else if (project_type == "fps") {
		project_type_info->set_text(
				String::utf8("•  ") + TTR("Complete playable FPS: mouse look, walking, jumping, obstacles.") +
				String::utf8("\n•  ") + TTR("Click to capture mouse. Arrows/WASD to move, Space to jump."));
	} else if (project_type == "hybrid") {
		project_type_info->set_text(
				String::utf8("•  ") + TTR("Osefe signature mode: 3D world + 2D HUD in one scene.") +
				String::utf8("\n•  ") + TTR("Camera, lighting, sky and a 2D UI layer.") +
				String::utf8("\n•  ") + TTR("Perfect for 2D-over-3D games (runner, AR-like, hybrid RPG)."));
	} else { // blank
		project_type_info->set_text(
				String::utf8("•  ") + TTR("Empty project, exactly like the classic behavior."));
	}

	_update_ok_button();
}

void ProjectDialog::_renderer_selected() {
	ERR_FAIL_NULL(renderer_button_group->get_pressed_button());

	String renderer_type = renderer_button_group->get_pressed_button()->get_meta(SNAME("rendering_method"));

	bool rd_error = false;

	if (renderer_type == "forward_plus") {
		renderer_info->set_text(
				String::utf8("•  ") + TTR("Supports desktop platforms only.") +
				String::utf8("\n•  ") + TTR("Advanced 3D graphics available.") +
				String::utf8("\n•  ") + TTR("Can scale to large complex scenes.") +
				String::utf8("\n•  ") + TTR("Uses RenderingDevice backend.") +
				String::utf8("\n•  ") + TTR("Slower rendering of simple scenes."));
		rd_error = !rendering_device_supported;
	} else if (renderer_type == "mobile") {
		renderer_info->set_text(
				String::utf8("•  ") + TTR("Supports desktop + mobile platforms.") +
				String::utf8("\n•  ") + TTR("Less advanced 3D graphics.") +
				String::utf8("\n•  ") + TTR("Less scalable for complex scenes.") +
				String::utf8("\n•  ") + TTR("Uses RenderingDevice backend.") +
				String::utf8("\n•  ") + TTR("Fast rendering of simple scenes."));
		rd_error = !rendering_device_supported;
	} else if (renderer_type == "gl_compatibility") {
		renderer_info->set_text(
				String::utf8("•  ") + TTR("Supports desktop, mobile + web platforms.") +
				String::utf8("\n•  ") + TTR("Least advanced 3D graphics.") +
				String::utf8("\n•  ") + TTR("Intended for low-end/older devices.") +
				String::utf8("\n•  ") + TTR("Uses OpenGL 3 backend (OpenGL 3.3/ES 3.0/WebGL2).") +
				String::utf8("\n•  ") + TTR("Fastest rendering of simple scenes."));
	} else {
		WARN_PRINT("Unknown renderer type. Please report this as a bug on GitHub.");
	}

	rd_not_supported->set_visible(rd_error);
	if (rd_error) {
		// Needs to be set here since theme colors aren't available at startup.
		rd_not_supported->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
		invalid_state_flags.set_flag(InvalidStateFlag::INVALID_STATE_FLAG_RENDERER_SELECT);
	} else {
		invalid_state_flags.clear_flag(InvalidStateFlag::INVALID_STATE_FLAG_RENDERER_SELECT);
	}

	_update_ok_button();
}

void ProjectDialog::_nonempty_confirmation_ok_pressed() {
	is_folder_empty = true;
	ok_pressed();
}

void ProjectDialog::ok_pressed() {
	// Before we create a project, check that the target folder is empty.
	// If not, we need to ask the user if they're sure they want to do this.
	if (!is_folder_empty) {
		if (!nonempty_confirmation) {
			nonempty_confirmation = memnew(ConfirmationDialog);
			nonempty_confirmation->set_title(TTRC("Warning: This folder is not empty"));
			nonempty_confirmation->set_text(TTRC("You are about to create an Osefe project in a non-empty folder.\nThe entire contents of this folder will be imported as project resources!\n\nAre you sure you wish to continue?"));
			nonempty_confirmation->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_nonempty_confirmation_ok_pressed));
			add_child(nonempty_confirmation);
		}
		nonempty_confirmation->popup_centered();
		return;
	}

	String path = project_path->get_text();

	// Osefe: ask before creating a starter scene — templates are optional, never forced.
	if (mode == MODE_NEW && !starter_confirmed && !starter_declined &&
			project_type_button_group.is_valid() && project_type_button_group->get_pressed_button()) {
		String selected_type = project_type_button_group->get_pressed_button()->get_meta(SNAME("project_type"));
		if (selected_type != "blank") {
			if (starter_confirmation == nullptr) {
				starter_confirmation = memnew(ConfirmationDialog);
				starter_confirmation->set_title(TTRC("Osefe Instant Games"));
				starter_confirmation->set_ok_button_text(TTRC("Yes, create the starter"));
				starter_confirmation->set_cancel_button_text(TTRC("No, empty project"));
				starter_confirmation->connect("confirmed", callable_mp(this, &ProjectDialog::_starter_confirmation_confirmed));
				starter_confirmation->connect("canceled", callable_mp(this, &ProjectDialog::_starter_confirmation_declined));
				add_child(starter_confirmation);
			}
			starter_confirmation->set_text(
					TTRC("Your project will start with a ready-to-run starter scene (scripts, physics, camera and HUD included). You can delete or modify it anytime.\n\nCreate the starter scene, or make an empty project?"));
			starter_confirmation->popup_centered();
			return;
		}
	}

	if (mode == MODE_NEW) {
		if (create_dir->is_pressed()) {
			Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			if (!d->dir_exists(path) && d->make_dir(path) != OK) {
				_set_message(TTRC("Couldn't create project directory, check permissions."), MESSAGE_ERROR);
				return;
			}
		}

		PackedStringArray project_features = ProjectSettings::get_required_features();
		ProjectSettings::CustomMap initial_settings;

		// Be sure to change this code if/when renderers are changed.
		// Default values are "forward_plus" for the main setting, "mobile" for the mobile override,
		// and "gl_compatibility" for the web override.
		String renderer_type = renderer_button_group->get_pressed_button()->get_meta(SNAME("rendering_method"));
		initial_settings["rendering/renderer/rendering_method"] = renderer_type;

		EditorSettings::get_singleton()->set("project_manager/default_renderer", renderer_type);
		EditorSettings::get_singleton()->save();

		if (renderer_type == "forward_plus") {
			project_features.push_back("Forward Plus");
		} else if (renderer_type == "mobile") {
			project_features.push_back("Mobile");
		} else if (renderer_type == "gl_compatibility") {
			project_features.push_back("GL Compatibility");
			// Also change the default renderer for the mobile override.
			initial_settings["rendering/renderer/rendering_method.mobile"] = "gl_compatibility";
		} else {
			WARN_PRINT("Unknown renderer type. Please report this as a bug on GitHub.");
		}

		project_features.sort();
		initial_settings["application/config/features"] = project_features;
		initial_settings["application/config/name"] = project_name->get_text().strip_edges();
		initial_settings["application/config/icon"] = "res://icon.svg";

		// Osefe: compute the starter scene for the selected project type before saving settings.
		String starter_scene_text;
		String starter_scene_path;
		String starter_script_path;
		String starter_script_text;
		if (project_type_button_group.is_valid() && project_type_button_group->get_pressed_button()) {
			String project_type = starter_declined ? String("blank") : (String)project_type_button_group->get_pressed_button()->get_meta(SNAME("project_type"));

			if (project_type == "2d") {
				starter_scene_path = "main_2d.tscn";
				starter_scene_text =
						"[gd_scene load_steps=2 format=3]\n"
						"\n"
						"[sub_resource type=\"LabelSettings\" id=\"LabelSettings_1\"]\n"
						"font_size = 32\n"
						"\n"
						"[node name=\"Main2D\" type=\"Node2D\"]\n"
						"\n"
						"[node name=\"HUD\" type=\"CanvasLayer\" parent=\".\"]\n"
						"\n"
						"[node name=\"Label\" type=\"Label\" parent=\"HUD\"]\n"
						"offset_left = 24.0\n"
						"offset_top = 24.0\n"
						"offset_right = 700.0\n"
						"offset_bottom = 80.0\n"
						"text = \"Osefe 2D — Press F5 to run!\"\n"
						"label_settings = SubResource(\"LabelSettings_1\")\n";
			} else if (project_type == "3d") {
				starter_scene_path = "main_3d.tscn";
				starter_scene_text =
						"[gd_scene load_steps=6 format=3]\n"
						"\n"
						"[sub_resource type=\"ProceduralSkyMaterial\" id=\"ProceduralSkyMaterial_1\"]\n"
						"\n"
						"[sub_resource type=\"Sky\" id=\"Sky_1\"]\n"
						"sky_material = SubResource(\"ProceduralSkyMaterial_1\")\n"
						"\n"
						"[sub_resource type=\"Environment\" id=\"Environment_1\"]\n"
						"background_mode = 2\n"
						"sky = SubResource(\"Sky_1\")\n"
						"\n"
						"[sub_resource type=\"PlaneMesh\" id=\"PlaneMesh_1\"]\n"
						"size = Vector2(10, 10)\n"
						"\n"
						"[sub_resource type=\"BoxMesh\" id=\"BoxMesh_1\"]\n"
						"size = Vector3(1, 1, 1)\n"
						"\n"
						"[node name=\"Main3D\" type=\"Node3D\"]\n"
						"\n"
						"[node name=\"WorldEnvironment\" type=\"WorldEnvironment\" parent=\".\"]\n"
						"environment = SubResource(\"Environment_1\")\n"
						"\n"
						"[node name=\"DirectionalLight3D\" type=\"DirectionalLight3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 0.866025, 0.5, 0, -0.5, 0.866025, 0, 10, 0)\n"
						"light_energy = 1.2\n"
						"shadow_enabled = true\n"
						"\n"
						"[node name=\"Camera3D\" type=\"Camera3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 2, 6)\n"
						"\n"
						"[node name=\"Ground\" type=\"MeshInstance3D\" parent=\".\"]\n"
						"mesh = SubResource(\"PlaneMesh_1\")\n"
						"\n"
						"[node name=\"Cube\" type=\"MeshInstance3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0.5, 0)\n"
						"mesh = SubResource(\"BoxMesh_1\")\n";
			} else if (project_type == "platformer") {
				starter_scene_path = "main_platformer.tscn";
				starter_script_path = "platformer_player.gd";
				starter_script_text =
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
				starter_scene_text =
						"[gd_scene load_steps=5 format=3]\n"
						"\n"
						"[ext_resource type=\"Script\" path=\"res://platformer_player.gd\" id=\"1_player\"]\n"
						"\n"
						"[sub_resource type=\"RectangleShape2D\" id=\"RectangleShape2D_ground\"]\n"
						"size = Vector2(1280, 80)\n"
						"\n"
						"[sub_resource type=\"RectangleShape2D\" id=\"RectangleShape2D_platform\"]\n"
						"size = Vector2(200, 20)\n"
						"\n"
						"[sub_resource type=\"RectangleShape2D\" id=\"RectangleShape2D_player\"]\n"
						"size = Vector2(40, 60)\n"
						"\n"
						"[node name=\"MainPlatformer\" type=\"Node2D\"]\n"
						"\n"
						"[node name=\"Ground\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(640, 440)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Ground\"]\n"
						"shape = SubResource(\"RectangleShape2D_ground\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Ground\"]\n"
						"offset_left = -640.0\n"
						"offset_top = -40.0\n"
						"offset_right = 640.0\n"
						"offset_bottom = 40.0\n"
						"color = Color(0.24, 0.2, 0.36, 1)\n"
						"\n"
						"[node name=\"Platform1\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(340, 340)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Platform1\"]\n"
						"shape = SubResource(\"RectangleShape2D_platform\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Platform1\"]\n"
						"offset_left = -100.0\n"
						"offset_top = -10.0\n"
						"offset_right = 100.0\n"
						"offset_bottom = 10.0\n"
						"color = Color(0.55, 0.4, 1, 1)\n"
						"\n"
						"[node name=\"Platform2\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(560, 260)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Platform2\"]\n"
						"shape = SubResource(\"RectangleShape2D_platform\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Platform2\"]\n"
						"offset_left = -100.0\n"
						"offset_top = -10.0\n"
						"offset_right = 100.0\n"
						"offset_bottom = 10.0\n"
						"color = Color(0.55, 0.4, 1, 1)\n"
						"\n"
						"[node name=\"Platform3\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(790, 190)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Platform3\"]\n"
						"shape = SubResource(\"RectangleShape2D_platform\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Platform3\"]\n"
						"offset_left = -100.0\n"
						"offset_top = -10.0\n"
						"offset_right = 100.0\n"
						"offset_bottom = 10.0\n"
						"color = Color(0.55, 0.4, 1, 1)\n"
						"\n"
						"[node name=\"Player\" type=\"CharacterBody2D\" parent=\".\"]\n"
						"position = Vector2(160, 360)\n"
						"script = ExtResource(\"1_player\")\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Player\"]\n"
						"shape = SubResource(\"RectangleShape2D_player\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Player\"]\n"
						"offset_left = -20.0\n"
						"offset_top = -30.0\n"
						"offset_right = 20.0\n"
						"offset_bottom = 30.0\n"
						"color = Color(0.9, 0.85, 1, 1)\n"
						"\n"
						"[node name=\"Camera2D\" type=\"Camera2D\" parent=\"Player\"]\n"
						"position_smoothing_enabled = true\n"
						"position_smoothing_speed = 8.0\n"
						"\n"
						"[node name=\"HUD\" type=\"CanvasLayer\" parent=\".\"]\n"
						"\n"
						"[node name=\"Label\" type=\"Label\" parent=\"HUD\"]\n"
						"offset_left = 24.0\n"
						"offset_top = 24.0\n"
						"offset_right = 760.0\n"
						"offset_bottom = 70.0\n"
						"text = \"Osefe Platformer — Arrow keys to move, Space to jump!\"\n"
						"theme_override_font_sizes/font_size = 24\n";
			} else if (project_type == "topdown") {
				starter_scene_path = "main_topdown.tscn";
				starter_script_path = "topdown_player.gd";
				starter_script_text =
						"extends CharacterBody2D\n"
						"\n"
						"const SPEED := 350.0\n"
						"var score := 0\n"
						"\n"
						"func _physics_process(_delta: float) -> void:\n"
						"\tvar direction := Vector2(Input.get_axis(\"ui_left\", \"ui_right\"), Input.get_axis(\"ui_up\", \"ui_down\"))\n"
						"\tvelocity = direction.normalized() * SPEED\n"
						"\tmove_and_slide()\n"
						"\tfor area in $PickupArea.get_overlapping_areas():\n"
						"\t\tif area.is_in_group(\"coin\"):\n"
						"\t\t\tarea.queue_free()\n"
						"\t\t\tscore += 1\n"
						"\t\t\tif score == 4:\n"
						"\t\t\t\t$HUD/Score.text = \"You win! All coins collected!\"\n"
						"\t\t\telse:\n"
						"\t\t\t\t$HUD/Score.text = \"Coins: %d / 4\" % score\n";
				starter_scene_text =
						"[gd_scene load_steps=7 format=3]\n"
						"\n"
						"[ext_resource type=\"Script\" path=\"res://topdown_player.gd\" id=\"1_player\"]\n"
						"\n"
						"[sub_resource type=\"CircleShape2D\" id=\"CircleShape2D_player\"]\n"
						"radius = 20.0\n"
						"\n"
						"[sub_resource type=\"CircleShape2D\" id=\"CircleShape2D_pickup\"]\n"
						"radius = 32.0\n"
						"\n"
						"[sub_resource type=\"CircleShape2D\" id=\"CircleShape2D_coin\"]\n"
						"radius = 12.0\n"
						"\n"
						"[sub_resource type=\"RectangleShape2D\" id=\"RectangleShape2D_wall_h\"]\n"
						"size = Vector2(1080, 40)\n"
						"\n"
						"[sub_resource type=\"RectangleShape2D\" id=\"RectangleShape2D_wall_v\"]\n"
						"size = Vector2(40, 680)\n"
						"\n"
						"[node name=\"MainTopDown\" type=\"Node2D\"]\n"
						"\n"
						"[node name=\"WallTop\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(540, -20)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"WallTop\"]\n"
						"shape = SubResource(\"RectangleShape2D_wall_h\")\n"
						"\n"
						"[node name=\"WallBottom\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(540, 660)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"WallBottom\"]\n"
						"shape = SubResource(\"RectangleShape2D_wall_h\")\n"
						"\n"
						"[node name=\"WallLeft\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(-20, 320)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"WallLeft\"]\n"
						"shape = SubResource(\"RectangleShape2D_wall_v\")\n"
						"\n"
						"[node name=\"WallRight\" type=\"StaticBody2D\" parent=\".\"]\n"
						"position = Vector2(1100, 320)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"WallRight\"]\n"
						"shape = SubResource(\"RectangleShape2D_wall_v\")\n"
						"\n"
						"[node name=\"Coin1\" type=\"Area2D\" parent=\".\" groups=[\"coin\"]]\n"
						"position = Vector2(160, 160)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Coin1\"]\n"
						"shape = SubResource(\"CircleShape2D_coin\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Coin1\"]\n"
						"offset_left = -12.0\n"
						"offset_top = -12.0\n"
						"offset_right = 12.0\n"
						"offset_bottom = 12.0\n"
						"color = Color(1, 0.84, 0.3, 1)\n"
						"\n"
						"[node name=\"Coin2\" type=\"Area2D\" parent=\".\" groups=[\"coin\"]]\n"
						"position = Vector2(920, 160)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Coin2\"]\n"
						"shape = SubResource(\"CircleShape2D_coin\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Coin2\"]\n"
						"offset_left = -12.0\n"
						"offset_top = -12.0\n"
						"offset_right = 12.0\n"
						"offset_bottom = 12.0\n"
						"color = Color(1, 0.84, 0.3, 1)\n"
						"\n"
						"[node name=\"Coin3\" type=\"Area2D\" parent=\".\" groups=[\"coin\"]]\n"
						"position = Vector2(160, 480)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Coin3\"]\n"
						"shape = SubResource(\"CircleShape2D_coin\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Coin3\"]\n"
						"offset_left = -12.0\n"
						"offset_top = -12.0\n"
						"offset_right = 12.0\n"
						"offset_bottom = 12.0\n"
						"color = Color(1, 0.84, 0.3, 1)\n"
						"\n"
						"[node name=\"Coin4\" type=\"Area2D\" parent=\".\" groups=[\"coin\"]]\n"
						"position = Vector2(920, 480)\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Coin4\"]\n"
						"shape = SubResource(\"CircleShape2D_coin\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Coin4\"]\n"
						"offset_left = -12.0\n"
						"offset_top = -12.0\n"
						"offset_right = 12.0\n"
						"offset_bottom = 12.0\n"
						"color = Color(1, 0.84, 0.3, 1)\n"
						"\n"
						"[node name=\"Player\" type=\"CharacterBody2D\" parent=\".\"]\n"
						"position = Vector2(540, 320)\n"
						"script = ExtResource(\"1_player\")\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Player\"]\n"
						"shape = SubResource(\"CircleShape2D_player\")\n"
						"\n"
						"[node name=\"PickupArea\" type=\"Area2D\" parent=\"Player\"]\n"
						"\n"
						"[node name=\"CollisionShape2D\" type=\"CollisionShape2D\" parent=\"Player/PickupArea\"]\n"
						"shape = SubResource(\"CircleShape2D_pickup\")\n"
						"\n"
						"[node name=\"Visual\" type=\"ColorRect\" parent=\"Player\"]\n"
						"offset_left = -20.0\n"
						"offset_top = -20.0\n"
						"offset_right = 20.0\n"
						"offset_bottom = 20.0\n"
						"color = Color(0.9, 0.85, 1, 1)\n"
						"\n"
						"[node name=\"Camera2D\" type=\"Camera2D\" parent=\"Player\"]\n"
						"position_smoothing_enabled = true\n"
						"position_smoothing_speed = 8.0\n"
						"\n"
						"[node name=\"HUD\" type=\"CanvasLayer\" parent=\"Player\"]\n"
						"\n"
						"[node name=\"Score\" type=\"Label\" parent=\"Player/HUD\"]\n"
						"offset_left = 24.0\n"
						"offset_top = 24.0\n"
						"offset_right = 760.0\n"
						"offset_bottom = 70.0\n"
						"text = \"Coins: 0 / 4\"\n"
						"theme_override_font_sizes/font_size = 24\n";
			} else if (project_type == "fps") {
				starter_scene_path = "main_fps.tscn";
				starter_script_path = "fps_player.gd";
				starter_script_text =
						"extends CharacterBody3D\n"
						"\n"
						"const SPEED := 6.0\n"
						"const JUMP_VELOCITY := 4.5\n"
						"const GRAVITY := 9.8\n"
						"var pitch := 0.0\n"
						"\n"
						"func _ready() -> void:\n"
						"\tInput.mouse_mode = Input.MOUSE_MODE_CAPTURED\n"
						"\n"
						"func _unhandled_input(event: InputEvent) -> void:\n"
						"\tif event is InputEventMouseMotion:\n"
						"\t\trotate_y(-event.relative.x * 0.002)\n"
						"\t\tpitch = clampf(pitch - event.relative.y * 0.002, -1.2, 1.2)\n"
						"\t\t$Camera3D.rotation.x = pitch\n"
						"\tif event.is_action_pressed(\"ui_cancel\"):\n"
						"\t\tInput.mouse_mode = Input.MOUSE_MODE_VISIBLE\n"
						"\n"
						"func _physics_process(delta: float) -> void:\n"
						"\tif not is_on_floor():\n"
						"\t\tvelocity.y -= GRAVITY * delta\n"
						"\tif Input.is_action_just_pressed(\"ui_accept\") and is_on_floor():\n"
						"\t\tvelocity.y = JUMP_VELOCITY\n"
						"\tvar input_dir := Input.get_vector(\"ui_left\", \"ui_right\", \"ui_up\", \"ui_down\")\n"
						"\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\n"
						"\tif direction:\n"
						"\t\tvelocity.x = direction.x * SPEED\n"
						"\t\tvelocity.z = direction.z * SPEED\n"
						"\telse:\n"
						"\t\tvelocity.x = move_toward(velocity.x, 0.0, SPEED)\n"
						"\t\tvelocity.z = move_toward(velocity.z, 0.0, SPEED)\n"
						"\tmove_and_slide()\n";
				starter_scene_text =
						"[gd_scene load_steps=10 format=3]\n"
						"\n"
						"[ext_resource type=\"Script\" path=\"res://fps_player.gd\" id=\"1_player\"]\n"
						"\n"
						"[sub_resource type=\"ProceduralSkyMaterial\" id=\"ProceduralSkyMaterial_1\"]\n"
						"\n"
						"[sub_resource type=\"Sky\" id=\"Sky_1\"]\n"
						"sky_material = SubResource(\"ProceduralSkyMaterial_1\")\n"
						"\n"
						"[sub_resource type=\"Environment\" id=\"Environment_1\"]\n"
						"background_mode = 2\n"
						"sky = SubResource(\"Sky_1\")\n"
						"\n"
						"[sub_resource type=\"BoxMesh\" id=\"BoxMesh_ground\"]\n"
						"size = Vector3(40, 1, 40)\n"
						"\n"
						"[sub_resource type=\"BoxShape3D\" id=\"BoxShape3D_ground\"]\n"
						"size = Vector3(40, 1, 40)\n"
						"\n"
						"[sub_resource type=\"BoxMesh\" id=\"BoxMesh_obstacle\"]\n"
						"size = Vector3(2, 2, 2)\n"
						"\n"
						"[sub_resource type=\"BoxShape3D\" id=\"BoxShape3D_obstacle\"]\n"
						"size = Vector3(2, 2, 2)\n"
						"\n"
						"[sub_resource type=\"CapsuleShape3D\" id=\"CapsuleShape3D_player\"]\n"
						"radius = 0.4\n"
						"height = 1.8\n"
						"\n"
						"[node name=\"MainFPS\" type=\"Node3D\"]\n"
						"\n"
						"[node name=\"WorldEnvironment\" type=\"WorldEnvironment\" parent=\".\"]\n"
						"environment = SubResource(\"Environment_1\")\n"
						"\n"
						"[node name=\"DirectionalLight3D\" type=\"DirectionalLight3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 0.866025, 0.5, 0, -0.5, 0.866025, 0, 10, 0)\n"
						"light_energy = 1.2\n"
						"shadow_enabled = true\n"
						"\n"
						"[node name=\"Ground\" type=\"StaticBody3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -0.5, 0)\n"
						"\n"
						"[node name=\"CollisionShape3D\" type=\"CollisionShape3D\" parent=\"Ground\"]\n"
						"shape = SubResource(\"BoxShape3D_ground\")\n"
						"\n"
						"[node name=\"Visual\" type=\"MeshInstance3D\" parent=\"Ground\"]\n"
						"mesh = SubResource(\"BoxMesh_ground\")\n"
						"\n"
						"[node name=\"Obstacle1\" type=\"StaticBody3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 4, 1, 0)\n"
						"\n"
						"[node name=\"CollisionShape3D\" type=\"CollisionShape3D\" parent=\"Obstacle1\"]\n"
						"shape = SubResource(\"BoxShape3D_obstacle\")\n"
						"\n"
						"[node name=\"Visual\" type=\"MeshInstance3D\" parent=\"Obstacle1\"]\n"
						"mesh = SubResource(\"BoxMesh_obstacle\")\n"
						"\n"
						"[node name=\"Obstacle2\" type=\"StaticBody3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, -3, 1, -2)\n"
						"\n"
						"[node name=\"CollisionShape3D\" type=\"CollisionShape3D\" parent=\"Obstacle2\"]\n"
						"shape = SubResource(\"BoxShape3D_obstacle\")\n"
						"\n"
						"[node name=\"Visual\" type=\"MeshInstance3D\" parent=\"Obstacle2\"]\n"
						"mesh = SubResource(\"BoxMesh_obstacle\")\n"
						"\n"
						"[node name=\"Obstacle3\" type=\"StaticBody3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, -6)\n"
						"\n"
						"[node name=\"CollisionShape3D\" type=\"CollisionShape3D\" parent=\"Obstacle3\"]\n"
						"shape = SubResource(\"BoxShape3D_obstacle\")\n"
						"\n"
						"[node name=\"Visual\" type=\"MeshInstance3D\" parent=\"Obstacle3\"]\n"
						"mesh = SubResource(\"BoxMesh_obstacle\")\n"
						"\n"
						"[node name=\"Player\" type=\"CharacterBody3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 3)\n"
						"script = ExtResource(\"1_player\")\n"
						"\n"
						"[node name=\"CollisionShape3D\" type=\"CollisionShape3D\" parent=\"Player\"]\n"
						"shape = SubResource(\"CapsuleShape3D_player\")\n"
						"\n"
						"[node name=\"Camera3D\" type=\"Camera3D\" parent=\"Player\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0.7, 0)\n"
						"\n"
						"[node name=\"HUD\" type=\"CanvasLayer\" parent=\".\"]\n"
						"\n"
						"[node name=\"Label\" type=\"Label\" parent=\"HUD\"]\n"
						"offset_left = 24.0\n"
						"offset_top = 24.0\n"
						"offset_right = 900.0\n"
						"offset_bottom = 70.0\n"
						"text = \"Osefe FPS — Mouse to look, Arrows/WASD to move, Space to jump, Esc to release mouse\"\n"
						"theme_override_font_sizes/font_size = 22\n";
			} else if (project_type == "hybrid") {
				starter_scene_path = "main_hybrid.tscn";
				starter_scene_text =
						"[gd_scene load_steps=7 format=3]\n"
						"\n"
						"[sub_resource type=\"ProceduralSkyMaterial\" id=\"ProceduralSkyMaterial_1\"]\n"
						"\n"
						"[sub_resource type=\"Sky\" id=\"Sky_1\"]\n"
						"sky_material = SubResource(\"ProceduralSkyMaterial_1\")\n"
						"\n"
						"[sub_resource type=\"Environment\" id=\"Environment_1\"]\n"
						"background_mode = 2\n"
						"sky = SubResource(\"Sky_1\")\n"
						"\n"
						"[sub_resource type=\"PlaneMesh\" id=\"PlaneMesh_1\"]\n"
						"size = Vector2(10, 10)\n"
						"\n"
						"[sub_resource type=\"BoxMesh\" id=\"BoxMesh_1\"]\n"
						"size = Vector3(1, 1, 1)\n"
						"\n"
						"[sub_resource type=\"LabelSettings\" id=\"LabelSettings_1\"]\n"
						"font_size = 28\n"
						"\n"
						"[node name=\"MainHybrid\" type=\"Node3D\"]\n"
						"\n"
						"[node name=\"WorldEnvironment\" type=\"WorldEnvironment\" parent=\".\"]\n"
						"environment = SubResource(\"Environment_1\")\n"
						"\n"
						"[node name=\"DirectionalLight3D\" type=\"DirectionalLight3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 0.866025, 0.5, 0, -0.5, 0.866025, 0, 10, 0)\n"
						"light_energy = 1.2\n"
						"shadow_enabled = true\n"
						"\n"
						"[node name=\"Camera3D\" type=\"Camera3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 2, 6)\n"
						"\n"
						"[node name=\"Ground\" type=\"MeshInstance3D\" parent=\".\"]\n"
						"mesh = SubResource(\"PlaneMesh_1\")\n"
						"\n"
						"[node name=\"Cube\" type=\"MeshInstance3D\" parent=\".\"]\n"
						"transform = Transform3D(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0.5, 0)\n"
						"mesh = SubResource(\"BoxMesh_1\")\n"
						"\n"
						"[node name=\"HUD\" type=\"CanvasLayer\" parent=\".\"]\n"
						"\n"
						"[node name=\"Title\" type=\"Label\" parent=\"HUD\"]\n"
						"offset_left = 24.0\n"
						"offset_top = 24.0\n"
						"offset_right = 800.0\n"
						"offset_bottom = 80.0\n"
						"text = \"Osefe Hybrid — 3D world + 2D UI, press F5!\"\n"
						"label_settings = SubResource(\"LabelSettings_1\")\n";
			}

			if (!starter_scene_path.is_empty()) {
				initial_settings["application/run/main_scene"] = "res://" + starter_scene_path;
			}

			// Remember the chosen project type for the next time.
			EditorSettings::get_singleton()->set("project_manager/default_project_type", project_type);
			EditorSettings::get_singleton()->save();
		}

		ProjectSettings::CustomMap extra_settings = EditorNode::get_initial_settings();
		for (const KeyValue<String, Variant> &extra_setting : extra_settings) {
			// Merge with other initial settings defined above.
			initial_settings[extra_setting.key] = extra_setting.value;
		}

		Error err = ProjectSettings::get_singleton()->save_custom(path.path_join("project.godot"), initial_settings, Vector<String>(), false);
		if (err != OK) {
			_set_message(TTRC("Couldn't create project.godot in project path."), MESSAGE_ERROR);
			return;
		}

		// Store default project icon in SVG format.
		Ref<FileAccess> fa_icon = FileAccess::open(path.path_join("icon.svg"), FileAccess::WRITE, &err);
		if (err != OK) {
			_set_message(TTRC("Couldn't create icon.svg in project path."), MESSAGE_ERROR);
			return;
		}
		fa_icon->store_string(get_default_project_icon());

		// Osefe: write the ready-to-run starter scene (and script) for the selected project type.
		if (!starter_scene_path.is_empty()) {
			if (!starter_script_path.is_empty()) {
				Ref<FileAccess> fa_script = FileAccess::open(path.path_join(starter_script_path), FileAccess::WRITE, &err);
				if (err != OK) {
					_set_message(TTRC("Couldn't create starter script in project path."), MESSAGE_ERROR);
					return;
				}
				fa_script->store_string(starter_script_text);
			}
			Ref<FileAccess> fa_scene = FileAccess::open(path.path_join(starter_scene_path), FileAccess::WRITE, &err);
			if (err != OK) {
				_set_message(TTRC("Couldn't create starter scene in project path."), MESSAGE_ERROR);
				return;
			}
			fa_scene->store_string(starter_scene_text);
		}

		EditorVCSInterface::create_vcs_metadata_files(EditorVCSInterface::VCSMetadata(vcs_metadata_selection->get_selected()), path);

		// Ensures external editors and IDEs use UTF-8 encoding.
		const String editor_config_path = path.path_join(".editorconfig");
		Ref<FileAccess> f = FileAccess::open(editor_config_path, FileAccess::WRITE);
		if (f.is_null()) {
			// .editorconfig isn't so critical.
			ERR_PRINT("Couldn't create .editorconfig in project path.");
		} else {
			f->store_line("root = true");
			f->store_line("");
			f->store_line("[*]");
			f->store_line("charset = utf-8");
			f->close();
			FileAccess::set_hidden_attribute(editor_config_path, true);
		}
	}

	// Two cases for importing a ZIP.
	switch (mode) {
		case MODE_IMPORT: {
			if (zip_path.is_empty()) {
				break;
			}

			path = install_path->get_text().simplify_path();
			[[fallthrough]];
		}
		case MODE_INSTALL: {
			ERR_FAIL_COND(zip_path.is_empty());

			Ref<FileAccess> io_fa;
			zlib_filefunc_def io = zipio_create_io(&io_fa);

			unzFile pkg = unzOpen2(zip_path.utf8().get_data(), &io);
			if (!pkg) {
				dialog_error->set_text(TTRC("Error opening package file, not in ZIP format."));
				dialog_error->popup_centered();
				return;
			}

			// Find the first directory with a "project.godot".
			String zip_root;
			int ret = unzGoToFirstFile(pkg);
			while (ret == UNZ_OK) {
				unz_file_info info;
				char fname[16384];
				unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
				ERR_FAIL_COND_MSG(ret != UNZ_OK, "Failed to get current file info.");

				String name = String::utf8(fname);

				// Skip the __MACOSX directory created by macOS's built-in file zipper.
				if (name.begins_with("__MACOSX")) {
					ret = unzGoToNextFile(pkg);
					continue;
				}

				if (name.get_file() == "project.godot") {
					zip_root = name.get_base_dir();
					break;
				}

				ret = unzGoToNextFile(pkg);
			}

			if (ret == UNZ_END_OF_LIST_OF_FILE) {
				_set_message(TTRC("Invalid \".zip\" project file; it doesn't contain a \"project.godot\" file."), MESSAGE_ERROR);
				unzClose(pkg);
				return;
			}

			if (create_dir->is_pressed()) {
				Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
				if (!d->dir_exists(path) && d->make_dir(path) != OK) {
					_set_message(TTRC("Couldn't create project directory, check permissions."), MESSAGE_ERROR);
					return;
				}
			}

			ret = unzGoToFirstFile(pkg);

			Vector<String> failed_files;
			while (ret == UNZ_OK) {
				//get filename
				unz_file_info info;
				char fname[16384];
				ret = unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);
				ERR_FAIL_COND_MSG(ret != UNZ_OK, "Failed to get current file info.");

				String name = String::utf8(fname);

				// Skip the __MACOSX directory created by macOS's built-in file zipper.
				if (name.begins_with("__MACOSX")) {
					ret = unzGoToNextFile(pkg);
					continue;
				}

				String rel_path = name.trim_prefix(zip_root);
				if (rel_path.is_empty()) { // Root.
				} else if (rel_path.ends_with("/")) { // Directory.
					Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
					da->make_dir(path.path_join(rel_path));
				} else { // File.
					Vector<uint8_t> uncomp_data;
					uncomp_data.resize(info.uncompressed_size);

					unzOpenCurrentFile(pkg);
					ret = unzReadCurrentFile(pkg, uncomp_data.ptrw(), uncomp_data.size());
					ERR_BREAK_MSG(ret < 0, vformat("An error occurred while attempting to read from file: %s. This file will not be used.", rel_path));
					unzCloseCurrentFile(pkg);

					Ref<FileAccess> f = FileAccess::open(path.path_join(rel_path), FileAccess::WRITE);
					if (f.is_valid()) {
						f->store_buffer(uncomp_data.ptr(), uncomp_data.size());
					} else {
						failed_files.push_back(rel_path);
					}
				}

				ret = unzGoToNextFile(pkg);
			}

			unzClose(pkg);

			if (failed_files.size()) {
				String err_msg = TTR("The following files failed extraction from package:") + "\n\n";
				for (int i = 0; i < failed_files.size(); i++) {
					if (i > 15) {
						err_msg += "\nAnd " + itos(failed_files.size() - i) + " more files.";
						break;
					}
					err_msg += failed_files[i] + "\n";
				}

				dialog_error->set_text(err_msg);
				dialog_error->popup_centered();
				return;
			}
		} break;
		default: {
		} break;
	}

	if (mode == MODE_DUPLICATE) {
		Ref<DirAccess> dir = DirAccess::open(original_project_path);
		Error err = FAILED;
		if (dir.is_valid()) {
			err = dir->copy_dir(".", path, -1, true);
		}
		if (err != OK) {
			dialog_error->set_text(vformat(TTR("Couldn't duplicate project (error %d)."), err));
			dialog_error->popup_centered();
			return;
		}
	}

	if (mode == MODE_RENAME || mode == MODE_INSTALL || mode == MODE_DUPLICATE) {
		// Load project.godot as ConfigFile to set the new name.
		ConfigFile cfg;
		String project_godot = path.path_join("project.godot");
		Error err = cfg.load(project_godot);
		if (err != OK) {
			dialog_error->set_text(vformat(TTR("Couldn't load project at '%s' (error %d). It may be missing or corrupted."), project_godot, err));
			dialog_error->popup_centered();
			return;
		}
		cfg.set_value("application", "config/name", project_name->get_text().strip_edges());
		err = cfg.save(project_godot);
		if (err != OK) {
			dialog_error->set_text(vformat(TTR("Couldn't save project at '%s' (error %d)."), project_godot, err));
			dialog_error->popup_centered();
			return;
		}
	}

	hide();
	if (mode == MODE_NEW || mode == MODE_IMPORT || mode == MODE_INSTALL) {
#ifdef ANDROID_ENABLED
		// Create a .nomedia file to hide assets from media apps on Android.
		// Android 11 has some issues with nomedia files, so it's disabled there. See GH-106479, GH-105399 for details.
		// NOTE: Nomedia file is also handled during the first filesystem scan. See editor_file_system.cpp -> EditorFileSystem::scan().
		String sdk_version = OS::get_singleton()->get_version().get_slicec('.', 0);
		if (sdk_version != "30") {
			const String nomedia_file_path = path.path_join(".nomedia");
			Ref<FileAccess> f2 = FileAccess::open(nomedia_file_path, FileAccess::WRITE);
			if (f2.is_null()) {
				// .nomedia isn't so critical.
				ERR_PRINT("Couldn't create .nomedia in project path.");
			} else {
				f2->close();
			}
		}
#endif
		emit_signal(SNAME("project_created"), path, edit_check_box->is_pressed());
	} else if (mode == MODE_DUPLICATE) {
		emit_signal(SNAME("project_duplicated"), original_project_path, path, edit_check_box->is_visible() && edit_check_box->is_pressed());
	} else if (mode == MODE_RENAME) {
		emit_signal(SNAME("projects_updated"));
	}
}

void ProjectDialog::set_zip_path(const String &p_path) {
	zip_path = p_path;
}

void ProjectDialog::set_zip_title(const String &p_title) {
	zip_title = p_title;
}

void ProjectDialog::set_original_project_path(const String &p_path) {
	original_project_path = p_path;
}

void ProjectDialog::set_duplicate_can_edit(bool p_duplicate_can_edit) {
	duplicate_can_edit = p_duplicate_can_edit;
}

void ProjectDialog::set_mode(Mode p_mode) {
	mode = p_mode;
}

void ProjectDialog::set_project_name(const String &p_name) {
	project_name->set_text(p_name);
}

void ProjectDialog::set_project_path(const String &p_path) {
	project_path->set_text(p_path);
}

void ProjectDialog::ask_for_path_and_show() {
	_reset_name();
	_browse_project_path();
}

void ProjectDialog::show_dialog(bool p_reset_name, bool p_is_confirmed) {
	_update_ok_button();

	if (mode == MODE_IMPORT && !p_is_confirmed) {
		return;
	}
	if (mode == MODE_RENAME) {
		// Name and path are set in `ProjectManager::_rename_project`.
		project_path->set_editable(false);

		set_title(TTRC("Rename Project"));
		set_ok_button_text(TTRC("Rename"));

		create_dir->hide();
		project_status_rect->hide();
		project_browse->hide();
		edit_check_box->hide();

		name_container->show();
		install_path_container->hide();
		renderer_container->hide();
		default_files_container->hide();

		callable_mp((Control *)project_name, &Control::grab_focus).call_deferred(false);
		callable_mp(project_name, &LineEdit::select_all).call_deferred();
	} else {
		if (p_reset_name) {
			_reset_name();
		}
		project_path->set_editable(true);

		if (mode == MODE_DUPLICATE) {
			String original_dir = original_project_path.get_base_dir();
			project_path->set_text(original_dir);
			install_path->set_text(original_dir);
			fdialog_project->set_current_dir(original_dir);
		} else {
			String fav_dir = EDITOR_GET("filesystem/directories/default_project_path");
			fav_dir = fav_dir.simplify_path();
			if (!fav_dir.is_empty()) {
				project_path->set_text(fav_dir);
				install_path->set_text(fav_dir);
				fdialog_project->set_current_dir(fav_dir);
			} else {
				Ref<DirAccess> d = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
				project_path->set_text(d->get_current_dir());
				install_path->set_text(d->get_current_dir());
				fdialog_project->set_current_dir(d->get_current_dir());
			}
		}

		create_dir->show();
		project_status_rect->show();
		project_browse->show();
		edit_check_box->show();

		if (mode == MODE_IMPORT) {
			set_title(TTRC("Import Existing Project"));
			set_ok_button_text(TTRC("Import"));

			name_container->hide();
			install_path_container->hide();
			renderer_container->hide();
			default_files_container->hide();

			// Project path dialog is also opened; no need to change focus.
		} else if (mode == MODE_NEW) {
			set_title(TTRC("Create New Project"));
			set_ok_button_text(TTRC("Create"));

			if (!rendering_device_checked) {
				rendering_device_supported = DisplayServer::is_rendering_device_supported();

				if (!rendering_device_supported) {
					List<BaseButton *> buttons;
					renderer_button_group->get_buttons(&buttons);
					for (BaseButton *button : buttons) {
						if (button->get_meta(SNAME("rendering_method")) == "gl_compatibility") {
							button->set_pressed(true);
							break;
						}
					}
				}
				_renderer_selected();
				rendering_device_checked = true;
			}

			name_container->show();
			install_path_container->hide();
			renderer_container->show();
			default_files_container->show();

			callable_mp((Control *)project_name, &Control::grab_focus).call_deferred(false);
			callable_mp(project_name, &LineEdit::select_all).call_deferred();
		} else if (mode == MODE_INSTALL) {
			set_title(TTR("Install Project:") + " " + zip_title);
			set_ok_button_text(TTRC("Install"));

			project_name->set_text(zip_title);

			name_container->show();
			install_path_container->hide();
			renderer_container->hide();
			default_files_container->hide();

			callable_mp((Control *)project_path, &Control::grab_focus).call_deferred(false);
		} else if (mode == MODE_DUPLICATE) {
			set_title(TTRC("Duplicate Project"));
			set_ok_button_text(TTRC("Duplicate"));

			name_container->show();
			install_path_container->hide();
			renderer_container->hide();
			default_files_container->hide();
			if (!duplicate_can_edit) {
				edit_check_box->hide();
			}

			callable_mp((Control *)project_name, &Control::grab_focus).call_deferred(false);
			callable_mp(project_name, &LineEdit::select_all).call_deferred();
		}

		auto_dir = "";
		last_custom_target_dir = "";
		_update_target_auto_dir();
		if (create_dir->is_pressed()) {
			// Append `auto_dir` to target path.
			_create_dir_toggled(true);
		}
	}

	_validate_path();

	popup_centered(Size2(500, 0) * EDSCALE);
}

void ProjectDialog::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_TRANSLATION_CHANGED: {
			if (rendering_device_checked) {
				_renderer_selected();
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			create_dir->set_button_icon(get_editor_theme_icon(SNAME("FolderCreate")));
			project_browse->set_button_icon(get_editor_theme_icon(SNAME("FolderBrowse")));
			install_browse->set_button_icon(get_editor_theme_icon(SNAME("FolderBrowse")));
		} break;
		case NOTIFICATION_READY: {
			fdialog_project = memnew(EditorFileDialog);
			fdialog_project->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
			fdialog_project->connect("dir_selected", callable_mp(this, &ProjectDialog::_project_path_selected));
			fdialog_project->connect("file_selected", callable_mp(this, &ProjectDialog::_project_path_selected));
			fdialog_project->connect("canceled", callable_mp(this, &ProjectDialog::show_dialog).bind(false, false), CONNECT_DEFERRED);
			callable_mp((Node *)this, &Node::add_sibling).call_deferred(fdialog_project, false);
		} break;
	}
}

void ProjectDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("project_created"));
	ADD_SIGNAL(MethodInfo("project_duplicated"));
	ADD_SIGNAL(MethodInfo("projects_updated"));
}

ProjectDialog::ProjectDialog() {
	VBoxContainer *vb = memnew(VBoxContainer);
	add_child(vb);

	name_container = memnew(VBoxContainer);
	vb->add_child(name_container);

	Label *l = memnew(Label);
	l->set_text(TTRC("Project Name:"));
	name_container->add_child(l);

	project_name = memnew(LineEdit);
	project_name->set_virtual_keyboard_show_on_focus(false);
	project_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	project_name->set_accessibility_name(TTRC("Project Name:"));
	name_container->add_child(project_name);

	project_path_container = memnew(VBoxContainer);
	vb->add_child(project_path_container);

	HBoxContainer *pphb_label = memnew(HBoxContainer);
	project_path_container->add_child(pphb_label);

	l = memnew(Label);
	l->set_text(TTRC("Project Path:"));
	l->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	pphb_label->add_child(l);

	create_dir = memnew(CheckButton);
	create_dir->set_text(TTRC("Create Folder"));
	create_dir->set_pressed(true);
	pphb_label->add_child(create_dir);
	create_dir->connect(SceneStringName(toggled), callable_mp(this, &ProjectDialog::_create_dir_toggled));

	HBoxContainer *pphb = memnew(HBoxContainer);
	project_path_container->add_child(pphb);

	project_path = memnew(LineEdit);
	project_path->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	project_path->set_accessibility_name(TTRC("Project Path:"));
	project_path->set_structured_text_bidi_override(TextServer::STRUCTURED_TEXT_FILE);
	pphb->add_child(project_path);

	install_path_container = memnew(VBoxContainer);
	vb->add_child(install_path_container);

	l = memnew(Label);
	l->set_text(TTRC("Project Installation Path:"));
	install_path_container->add_child(l);

	HBoxContainer *iphb = memnew(HBoxContainer);
	install_path_container->add_child(iphb);

	install_path = memnew(LineEdit);
	install_path->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	install_path->set_accessibility_name(TTRC("Project Installation Path:"));
	install_path->set_structured_text_bidi_override(TextServer::STRUCTURED_TEXT_FILE);
	iphb->add_child(install_path);

	// status icon
	project_status_rect = memnew(TextureRect);
	project_status_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	pphb->add_child(project_status_rect);

	project_browse = memnew(Button);
	project_browse->set_text(TTRC("Browse"));
	project_browse->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_browse_project_path));
	pphb->add_child(project_browse);

	// install status icon
	install_status_rect = memnew(TextureRect);
	install_status_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	iphb->add_child(install_status_rect);

	install_browse = memnew(Button);
	install_browse->set_text(TTRC("Browse"));
	install_browse->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_browse_install_path));
	iphb->add_child(install_browse);

	msg = memnew(Label);
	msg->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	msg->set_accessibility_live(AccessibilityServerEnums::LIVE_POLITE);
	msg->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	msg->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	msg->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	vb->add_child(msg);

	// Project type selection (Osefe: 2D / 3D / Hybrid / Blank).
	project_type_container = memnew(VBoxContainer);
	vb->add_child(project_type_container);
	l = memnew(Label);
	l->set_text(TTRC("Project Type:"));
	project_type_container->add_child(l);
	HBoxContainer *ptshc = memnew(HBoxContainer);
	project_type_container->add_child(ptshc);

	String default_project_type = "hybrid";
	if (EditorSettings::get_singleton()->has_setting("project_manager/default_project_type")) {
		default_project_type = EditorSettings::get_singleton()->get_setting("project_manager/default_project_type");
	}

	Container *ptvb = memnew(VBoxContainer);
	ptshc->add_child(ptvb);
	project_type_button_group.instantiate();

	Button *pt_button = memnew(CheckBox);
	pt_button->set_button_group(project_type_button_group);
	pt_button->set_text(TTRC("2D"));
	pt_button->set_meta(SNAME("project_type"), "2d");
	pt_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_project_type_selected));
	ptvb->add_child(pt_button);
	if (default_project_type == "2d") {
		pt_button->set_pressed(true);
	}
	pt_button = memnew(CheckBox);
	pt_button->set_button_group(project_type_button_group);
	pt_button->set_text(TTRC("3D"));
	pt_button->set_meta(SNAME("project_type"), "3d");
	pt_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_project_type_selected));
	ptvb->add_child(pt_button);
	if (default_project_type == "3d") {
		pt_button->set_pressed(true);
	}
	pt_button = memnew(CheckBox);
	pt_button->set_button_group(project_type_button_group);
	pt_button->set_text(TTRC("Hybrid (2D + 3D)"));
	pt_button->set_meta(SNAME("project_type"), "hybrid");
	pt_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_project_type_selected));
	ptvb->add_child(pt_button);
	if (default_project_type == "hybrid") {
		pt_button->set_pressed(true);
	}
	pt_button = memnew(CheckBox);
	pt_button->set_button_group(project_type_button_group);
	pt_button->set_text(TTRC("Platformer 2D (ready game)"));
	pt_button->set_meta(SNAME("project_type"), "platformer");
	pt_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_project_type_selected));
	ptvb->add_child(pt_button);
	if (default_project_type == "platformer") {
		pt_button->set_pressed(true);
	}
	pt_button = memnew(CheckBox);
	pt_button->set_button_group(project_type_button_group);
	pt_button->set_text(TTRC("Top-Down 2D (ready game)"));
	pt_button->set_meta(SNAME("project_type"), "topdown");
	pt_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_project_type_selected));
	ptvb->add_child(pt_button);
	if (default_project_type == "topdown") {
		pt_button->set_pressed(true);
	}
	pt_button = memnew(CheckBox);
	pt_button->set_button_group(project_type_button_group);
	pt_button->set_text(TTRC("FPS 3D (ready game)"));
	pt_button->set_meta(SNAME("project_type"), "fps");
	pt_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_project_type_selected));
	ptvb->add_child(pt_button);
	if (default_project_type == "fps") {
		pt_button->set_pressed(true);
	}
	pt_button = memnew(CheckBox);
	pt_button->set_button_group(project_type_button_group);
	pt_button->set_text(TTRC("Blank"));
	pt_button->set_meta(SNAME("project_type"), "blank");
	pt_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_project_type_selected));
	ptvb->add_child(pt_button);
	if (default_project_type == "blank") {
		pt_button->set_pressed(true);
	}

	ptshc->add_child(memnew(VSeparator));

	// Right hand side, used for text explaining each choice.
	ptvb = memnew(VBoxContainer);
	ptvb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	ptshc->add_child(ptvb);
	project_type_info = memnew(Label);
	project_type_info->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	project_type_info->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	project_type_info->set_modulate(Color(1, 1, 1, 0.7));
	ptvb->add_child(project_type_info);
	// Initialize the description for the pressed (or default) type.
	if (project_type_button_group->get_pressed_button()) {
		_project_type_selected();
	}

	l = memnew(Label);
	l->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	l->set_text(TTRC("The project type creates a ready-to-run starter scene for you."));
	l->set_custom_minimum_size(Size2(0, 20) * EDSCALE);
	l->set_modulate(Color(1, 1, 1, 0.7));
	project_type_container->add_child(l);

	// Renderer selection.
	renderer_container = memnew(VBoxContainer);
	vb->add_child(renderer_container);
	l = memnew(Label);
	l->set_text(TTRC("Renderer:"));
	renderer_container->add_child(l);
	HBoxContainer *rshc = memnew(HBoxContainer);
	renderer_container->add_child(rshc);
	renderer_button_group.instantiate();

	// Left hand side, used for checkboxes to select renderer.
	Container *rvb = memnew(VBoxContainer);
	rshc->add_child(rvb);

	String default_renderer_type = "forward_plus";
	if (EditorSettings::get_singleton()->has_setting("project_manager/default_renderer")) {
		default_renderer_type = EditorSettings::get_singleton()->get_setting("project_manager/default_renderer");
	}

	Button *rs_button = memnew(CheckBox);
	rs_button->set_button_group(renderer_button_group);
	rs_button->set_text(TTRC("Forward+"));
	rs_button->set_accessibility_name(TTRC("Renderer:"));
#ifndef RD_ENABLED
	rs_button->set_disabled(true);
#endif
	rs_button->set_meta(SNAME("rendering_method"), "forward_plus");
	rs_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_renderer_selected));
	rvb->add_child(rs_button);
	if (default_renderer_type == "forward_plus") {
		rs_button->set_pressed(true);
	}
	rs_button = memnew(CheckBox);
	rs_button->set_button_group(renderer_button_group);
	rs_button->set_text(TTRC("Mobile"));
	rs_button->set_accessibility_name(TTRC("Renderer:"));
#ifndef RD_ENABLED
	rs_button->set_disabled(true);
#endif
	rs_button->set_meta(SNAME("rendering_method"), "mobile");
	rs_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_renderer_selected));
	rvb->add_child(rs_button);
	if (default_renderer_type == "mobile") {
		rs_button->set_pressed(true);
	}
	rs_button = memnew(CheckBox);
	rs_button->set_button_group(renderer_button_group);
	rs_button->set_text(TTRC("Compatibility"));
	rs_button->set_accessibility_name(TTRC("Renderer:"));
#if !defined(GLES3_ENABLED)
	rs_button->set_disabled(true);
#endif
	rs_button->set_meta(SNAME("rendering_method"), "gl_compatibility");
	rs_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectDialog::_renderer_selected));
	rvb->add_child(rs_button);
	LinkButton *ri_link = memnew(LinkButton);
	ri_link->set_text(TTRC("More information"));
	ri_link->set_uri(GODOT_VERSION_DOCS_URL "/tutorials/rendering/renderers.html");
	ri_link->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	rvb->add_child(ri_link);
#if defined(GLES3_ENABLED)
	if (default_renderer_type == "gl_compatibility") {
		rs_button->set_pressed(true);
	}
#endif
	rshc->add_child(memnew(VSeparator));

	// Right hand side, used for text explaining each choice.
	rvb = memnew(VBoxContainer);
	rvb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rshc->add_child(rvb);
	renderer_info = memnew(Label);
	renderer_info->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	renderer_info->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	renderer_info->set_modulate(Color(1, 1, 1, 0.7));
	rvb->add_child(renderer_info);

	rd_not_supported = memnew(Label);
	rd_not_supported->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	rd_not_supported->set_text(vformat(TTR("RenderingDevice-based methods not available on this GPU:\n%s\nPlease use the Compatibility renderer."), RenderingServer::get_singleton()->get_video_adapter_name()));
	rd_not_supported->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	rd_not_supported->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	rd_not_supported->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	rd_not_supported->set_visible(false);
	renderer_container->add_child(rd_not_supported);

	l = memnew(Label);
	l->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	l->set_text(TTRC("The renderer can be changed later, but scenes may need to be adjusted."));
	// Add some extra spacing to separate it from the list above and the buttons below.
	l->set_custom_minimum_size(Size2(0, 40) * EDSCALE);
	l->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	l->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	l->set_modulate(Color(1, 1, 1, 0.7));
	renderer_container->add_child(l);

	default_files_container = memnew(HBoxContainer);
	vb->add_child(default_files_container);
	l = memnew(Label);
	l->set_text(TTRC("Version Control Metadata:"));
	default_files_container->add_child(l);
	vcs_metadata_selection = memnew(OptionButton);
	vcs_metadata_selection->set_custom_minimum_size(Size2(100, 20));
	vcs_metadata_selection->add_item(TTRC("None"), (int)EditorVCSInterface::VCSMetadata::NONE);
	vcs_metadata_selection->add_item(TTRC("Git"), (int)EditorVCSInterface::VCSMetadata::GIT);
	vcs_metadata_selection->select((int)EditorVCSInterface::VCSMetadata::GIT);
	vcs_metadata_selection->set_accessibility_name(TTRC("Version Control Metadata:"));
	default_files_container->add_child(vcs_metadata_selection);
	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	default_files_container->add_child(spacer);
	fdialog_install = memnew(EditorFileDialog);
	fdialog_install->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	add_child(fdialog_install);

	Control *spacer2 = memnew(Control);
	spacer2->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vb->add_child(spacer2);

	edit_check_box = memnew(CheckBox);
	edit_check_box->set_text(TTRC("Edit Now"));
	edit_check_box->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	edit_check_box->set_pressed(true);
	vb->add_child(edit_check_box);

	project_name->connect(SceneStringName(text_changed), callable_mp(this, &ProjectDialog::_project_name_changed).unbind(1));
	project_name->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectDialog::ok_pressed).unbind(1));

	project_path->connect(SceneStringName(text_changed), callable_mp(this, &ProjectDialog::_project_path_changed).unbind(1));
	project_path->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectDialog::ok_pressed).unbind(1));

	install_path->connect(SceneStringName(text_changed), callable_mp(this, &ProjectDialog::_install_path_changed).unbind(1));
	install_path->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectDialog::ok_pressed).unbind(1));

	fdialog_install->connect("dir_selected", callable_mp(this, &ProjectDialog::_install_path_selected));
	fdialog_install->connect("file_selected", callable_mp(this, &ProjectDialog::_install_path_selected));

	set_hide_on_ok(false);

	dialog_error = memnew(AcceptDialog);
	add_child(dialog_error);
}
