import bpy
from pathlib import Path
import sys
root = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
root.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
mesh = bpy.data.meshes.new('Triangle')
mesh.from_pydata([(0,0,0),(1,0,0),(0,1,0)], [], [(0,1,2)])
mesh.uv_layers.new()
obj = bpy.data.objects.new('Triangle', mesh)
bpy.context.collection.objects.link(obj)
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
material = bpy.data.materials.new('TestMaterial')
material.use_nodes = True
image = bpy.data.images.new('PackedColor', width=2, height=2)
image.generated_color = (0.8,0.2,0.1,1)
image.pack()
texture = material.node_tree.nodes.new('ShaderNodeTexImage')
texture.image = image
material.node_tree.links.new(texture.outputs['Color'], material.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
obj.data.materials.append(material)
obj.keyframe_insert(data_path='location', frame=1)
obj.location.x = 2
obj.keyframe_insert(data_path='location', frame=10)
bpy.context.scene.frame_end = 10
bpy.context.scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(root/'triangle.blend'))
bpy.ops.export_scene.gltf(filepath=str(root/'triangle.glb'), export_format='GLB')
bpy.ops.export_scene.gltf(filepath=str(root/'triangle.gltf'), export_format='GLTF_SEPARATE')
bpy.ops.wm.stl_export(filepath=str(root/'triangle.stl'))
(root/'triangle.obj').write_text('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n')
(root/'triangle.dae').write_text('''<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
<asset><created>2026-09-12T00:00:00Z</created><modified>2026-09-12T00:00:00Z</modified><unit meter="1"/><up_axis>Y_UP</up_axis></asset>
<library_geometries><geometry id="Triangle"><mesh>
<source id="positions"><float_array id="positions-array" count="9">0 0 0 1 0 0 0 1 0</float_array><technique_common><accessor source="#positions-array" count="3" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
<vertices id="vertices"><input semantic="POSITION" source="#positions"/></vertices><triangles count="1"><input semantic="VERTEX" source="#vertices" offset="0"/><p>0 1 2</p></triangles>
</mesh></geometry></library_geometries><library_visual_scenes><visual_scene id="Scene"><node id="TriangleNode"><instance_geometry url="#Triangle"/></node></visual_scene></library_visual_scenes><scene><instance_visual_scene url="#Scene"/></scene></COLLADA>''')

# A skinned mesh triggers Blender's glTF bone display helpers on import.
armature = bpy.data.armatures.new('TestRig')
rig = bpy.data.objects.new('TestRig', armature)
bpy.context.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
obj.select_set(False)
rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bone = armature.edit_bones.new('RootBone')
bone.head = (0, 0, 0)
bone.tail = (0, 0, 1)
bpy.ops.object.mode_set(mode='OBJECT')
weights = obj.vertex_groups.new(name='RootBone')
weights.add(list(range(len(mesh.vertices))), 1.0, 'REPLACE')
modifier = obj.modifiers.new('Skin', 'ARMATURE')
modifier.object = rig
obj.parent = rig
bpy.ops.export_scene.gltf(filepath=str(root/'rigged_triangle.glb'), export_format='GLB')
bpy.ops.export_scene.gltf(filepath=str(root/'rigged_triangle.gltf'), export_format='GLTF_SEPARATE')
