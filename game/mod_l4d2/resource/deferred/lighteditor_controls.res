"Resource/Deferred/lighteditor_controls.res"
{
	"LightEditorControls"
	{
		"ControlName"		"Frame"
		"fieldName"			"LightEditorControls"
		"xpos"				"8"
		"ypos"				"200"
		"wide"				"180"
		"tall"				"214"
		"autoResize"		"0"
		"pinCorner"			"0"
		"visible"			"1"
		"enabled"			"1"
		"tabPosition"		"0"
		"settitlebarvisible"	"1"
		"title"				"Lights"
	}

	"action_select"
	{
		"ControlName"		"RadioButton"
		"fieldName"			"action_select"
		"xpos"				"8"
		"ypos"				"28"
		"wide"				"164"
		"tall"				"22"
		"labelText"			"Select (X)"
		"textAlignment"		"west"
		"tabPosition"		"1"
		"SubTabPosition"	"1"
		"Default"			"1"
	}

	"action_add"
	{
		"ControlName"		"RadioButton"
		"fieldName"			"action_add"
		"xpos"				"8"
		"ypos"				"52"
		"wide"				"164"
		"tall"				"22"
		"labelText"			"Add (B)"
		"textAlignment"		"west"
		"tabPosition"		"1"
		"SubTabPosition"	"2"
	}

	"action_translate"
	{
		"ControlName"		"RadioButton"
		"fieldName"			"action_translate"
		"xpos"				"8"
		"ypos"				"76"
		"wide"				"164"
		"tall"				"22"
		"labelText"			"Translate (V)"
		"textAlignment"		"west"
		"tabPosition"		"1"
		"SubTabPosition"	"3"
	}

	"action_rotate"
	{
		"ControlName"		"RadioButton"
		"fieldName"			"action_rotate"
		"xpos"				"8"
		"ypos"				"100"
		"wide"				"164"
		"tall"				"22"
		"labelText"			"Rotate (C)"
		"textAlignment"		"west"
		"tabPosition"		"1"
		"SubTabPosition"	"4"
	}

	"button_save"
	{
		"ControlName"		"Button"
		"fieldName"			"button_save"
		"xpos"				"8"
		"ypos"				"132"
		"wide"				"164"
		"tall"				"22"
		"labelText"			"Save"
		"textAlignment"		"center"
		"command"			"savelights"
	}

	"button_reload"
	{
		"ControlName"		"Button"
		"fieldName"			"button_reload"
		"xpos"				"8"
		"ypos"				"156"
		"wide"				"164"
		"tall"				"22"
		"labelText"			"Reload"
		"textAlignment"		"center"
		"command"			"reloadlights"
	}

	"button_props"
	{
		"ControlName"		"Button"
		"fieldName"			"button_props"
		"xpos"				"8"
		"ypos"				"180"
		"wide"				"164"
		"tall"				"22"
		"labelText"			"Properties"
		"textAlignment"		"center"
		"command"			"toggleprops"
	}
}
