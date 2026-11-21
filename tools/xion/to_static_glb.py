import bpy, sys
src, dst = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
for ob in [o for o in bpy.data.objects if o.type == 'MESH']:          # bake the pose into plain meshes, drop the skin
    bpy.context.view_layer.objects.active = ob
    for m in list(ob.modifiers):
        if m.type == 'ARMATURE': bpy.ops.object.modifier_apply(modifier=m.name)
    ob.parent = None
    for vg in list(ob.vertex_groups): ob.vertex_groups.remove(vg)
for ob in [o for o in bpy.data.objects if o.type in ('ARMATURE', 'EMPTY')]: bpy.data.objects.remove(ob, do_unlink=True)
bpy.ops.export_scene.gltf(filepath=dst, export_format='GLB', export_skins=False, export_animations=False, export_morph=False)
print('meshes', len([o for o in bpy.data.objects if o.type == 'MESH']), 'tris', sum(len(o.data.polygons) for o in bpy.data.objects if o.type == 'MESH'))
