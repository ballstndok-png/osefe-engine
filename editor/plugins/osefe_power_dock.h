/**************************************************************************/
/*  osefe_power_dock.h                                                     */
/**************************************************************************/

#ifndef OSEFE_POWER_DOCK_H
#define OSEFE_POWER_DOCK_H

#include "editor/plugins/editor_plugin.h"

class Button;
class VBoxContainer;

// Osefe Power Dock: one-click generators (SFX, 2D level, character) and a
// Turbo button that applies performance project settings. GDScript-friendly.
class OsefePowerDockPlugin : public EditorPlugin {
	GDCLASS(OsefePowerDockPlugin, EditorPlugin);

private:
	VBoxContainer *dock = nullptr;

	void _generate_sfx();
	void _generate_level();
	void _generate_character();
	void _apply_turbo();

public:
	String get_plugin_name() const;

	void _notification(int p_what);

	OsefePowerDockPlugin();
};

#endif // OSEFE_POWER_DOCK_H
