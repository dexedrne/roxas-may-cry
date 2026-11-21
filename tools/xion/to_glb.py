import bpy, sys, os
src, dst = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
tex_dir = os.path.join(os.path.dirname(src), 'mat0')
loaded = 0
for m in bpy.data.materials:                               # link base-colour textures by name if the export left them unlinked
    if not m.use_nodes: continue
    bsdf = next((n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if not bsdf or bsdf.inputs['Base Color'].is_linked: continue
    stem = m.name.split('_mat')[0]
    for f in sorted(os.listdir(tex_dir)) if os.path.isdir(tex_dir) else []:
        if f.endswith('.png') and '_0d0' in f and f.startswith(stem[:7]):
            img = bpy.data.images.load(os.path.join(tex_dir, f)); t = m.node_tree.nodes.new('ShaderNodeTexImage'); t.image = img
            m.node_tree.links.new(t.outputs['Color'], bsdf.inputs['Base Color']); loaded += 1; break
bpy.ops.export_scene.gltf(filepath=dst, export_format='GLB')
print('materials', len(bpy.data.materials), 'textures linked', loaded, 'meshes', len(bpy.data.meshes))
