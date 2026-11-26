// Copyright Zetta 3D. See LICENSE.md.
// Every built-in Unreal tool the agent can call, by area. Names, inputs and results follow the Unreal agent protocol.

#include "ZettaAgentTools.h"
#include "ZettaLogBuffer.h"
#include "ZettaToolCommon.h"

void ZettaRegisterAgentTools(FZettaAgentTools& Tools)
{
	// First, so the Output Log is kept from the moment the editor loads the plugin (output_log, the snapshot, compile).
	ZettaLogBuffer::Start();

	ZettaToolAreas::RegisterProjectTools(Tools);   // unreal_project_info, api_lookup
	ZettaToolAreas::RegisterInspectTools(Tools);   // level_outline, inspect_actor, find_actors, get_selection
	ZettaToolAreas::RegisterLevelTools(Tools);     // spawn/modify/delete actors, components, set_properties, level, PIE, console, viewport, undo
	ZettaToolAreas::RegisterBlueprintTools(Tools); // create_blueprint, edit_blueprint
	ZettaToolAreas::RegisterAssetTools(Tools);     // find_assets, inspect_asset, asset_operation, import_file, delete_asset
	ZettaToolAreas::RegisterMaterialTools(Tools);  // create_material, assign_material
	ZettaToolAreas::RegisterFileTools(Tools);      // read_file, search_files, write_file, edit_file, output_log
	ZettaToolAreas::RegisterCompileTools(Tools);   // compile
	ZettaToolAreas::RegisterCaptureTools(Tools);   // capture_view
	ZettaToolAreas::RegisterPythonTools(Tools);    // run_python
	ZettaToolAreas::RegisterZettaTools(Tools);     // generate_3d_model, process_model, import_generation, list_generations
	ZettaToolAreas::RegisterImageTools(Tools);     // generate_image_with_references, edit_image, compose_images, list_images, save_image_asset
	ZettaToolAreas::RegisterPipelineTools(Tools);  // list/get/build/edit/run pipelines, run status, manage runs, import results
}
