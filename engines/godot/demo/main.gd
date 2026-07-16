# SPDX-License-Identifier: MPL-2.0
# Demo of the Mecojoni GDExtension.
#
# In the editor (or a normal run) it shows a small UI: click the buttons to
# generate greetings from data/hello.meco and NPC lines from the multi-module
# data/npc package, feeding the playerName input from the text field.
#
# When run headless (godot --headless --path .) it prints a fixed-seed
# console demo instead and quits, so the integration can be tested from CI.
extends Control

var hello_grammar: MecoGrammar
var npc_grammar: MecoGrammar

@onready var output: RichTextLabel = $Margin/VBox/Output
@onready var hello_button: Button = $Margin/VBox/Controls/HelloButton
@onready var npc_button: Button = $Margin/VBox/Controls/NpcButton
@onready var name_edit: LineEdit = $Margin/VBox/Controls/NameEdit


func _ready() -> void:
	_log("Mecojoni versions: %s" % MecoGrammar.versions())
	hello_grammar = _load_hello()
	npc_grammar = _load_npc()

	if DisplayServer.get_name() == "headless":
		_console_demo()
		get_tree().quit()
		return

	hello_button.pressed.connect(_generate_hello)
	npc_button.pressed.connect(_generate_npc)
	if hello_grammar != null:
		_log("hello.meco compiled: entries %s, default \"%s\"" % [
			hello_grammar.get_entries(), hello_grammar.get_default_entry(),
		])
		_generate_hello()
	if npc_grammar != null:
		_generate_npc()


# --- grammar loading -------------------------------------------------------

func _load_hello() -> MecoGrammar:
	var grammar := MecoGrammar.new()
	if grammar.compile_file("res://data/hello.meco") != OK:
		_log("ERROR compiling hello.meco: %s" % grammar.get_error_message())
		return null
	for warning in grammar.get_warnings():
		_log("warning: %s" % warning)

	# Prove the .mecob artifact path: encode in-memory, reload, compare. A
	# game would normally ship artifacts precompiled with the authoring CLI
	# and call load_artifact_file("res://data/hello.mecob") directly.
	var bytes := grammar.encode_artifact(0)
	var reloaded := MecoGrammar.new()
	if bytes.is_empty() or reloaded.load_artifact(bytes) != OK:
		_log("ERROR round-tripping artifact: %s" % reloaded.get_error_message())
		return grammar
	assert(grammar.generate(42).text == reloaded.generate(42).text)
	_log("bytecode/1 artifact round-trip OK (%d bytes)" % bytes.size())
	return reloaded


func _load_npc() -> MecoGrammar:
	var builder := MecoPackageBuilder.new()
	if builder.add_module_file("root", "res://data/npc/root.meco") != OK \
			or builder.add_module_file("common", "res://data/npc/common.meco") != OK \
			or builder.resolve_import("root", "./common.meco", "common") != OK:
		_log("ERROR assembling npc package: %s" % builder.get_error_message())
		return null
	var grammar := builder.compile()
	if grammar == null:
		_log("ERROR compiling npc package: %s" % builder.get_error_message())
	return grammar


# --- generation ------------------------------------------------------------

func _generate_hello() -> void:
	if hello_grammar == null:
		return
	var gen_seed := randi()
	var result := hello_grammar.generate(gen_seed)
	if result.ok:
		_log("greeting (seed %d): %s" % [gen_seed, result.text])
	else:
		_log("ERROR %s: %s" % [result.error_code, result.error_message])


func _generate_npc() -> void:
	if npc_grammar == null:
		return
	var gen_seed := randi()
	var result := npc_grammar.generate(gen_seed, "", {"playerName": name_edit.text})
	if result.ok:
		_log("npc (seed %d): %s" % [gen_seed, result.text])
	else:
		_log("ERROR %s: %s" % [result.error_code, result.error_message])


func _log(line: String) -> void:
	print(line)
	output.add_text(line + "\n")


# --- deterministic console demo for headless/CI runs ------------------------

func _console_demo() -> void:
	if hello_grammar == null or npc_grammar == null:
		push_error("grammars failed to load")
		return
	for gen_seed in 5:
		print("hello seed %d -> %s" % [gen_seed, hello_grammar.generate(gen_seed).text])
	for gen_seed in 3:
		var result := npc_grammar.generate(gen_seed, "", {"playerName": "Ripley"})
		print("npc seed %d -> %s" % [gen_seed, result.text])
