"""Verify controller-role parity, grip-pose source paths, guide labels and deployment inputs."""
import json,pathlib
root=pathlib.Path(__file__).resolve().parents[3]
mod=root/'mods/yapyap-vr'
guide=json.loads((root/'config/controller-maps/yapyap.json').read_text())
manifest=json.loads((mod/'actions.json').read_text())
actions={a['name'].lower():a['type'] for a in manifest['actions']}
devices={d['id']:d for d in guide['devices']}
q=json.loads((mod/'bindings_oculus_touch.json').read_text())
k=json.loads((mod/'bindings_knuckles.json').read_text())
for b,controller in [(q,'oculus_touch'),(k,'knuckles')]:
 assert b['controller_type']==controller
 xbox=b['bindings']['/actions/xbox']
 assert len(xbox['sources'])==10
 for source in xbox['sources']:
  for feature,data in source['inputs'].items():
   output=data['output'].lower();assert output in actions,output
   assert actions[output]==('vector2' if feature=='position' else 'vector1' if feature=='pull' else 'boolean'),output
 assert {(p['output'].lower(),p['path']) for p in xbox['poses']}=={
  ('/actions/xbox/in/leftgrippose','/user/hand/left/pose/handgrip'),
  ('/actions/xbox/in/rightgrippose','/user/hand/right/pose/handgrip')}
 for p in xbox['poses']:assert actions[p['output'].lower()]=='pose'
 assert not any(c['output'].lower().endswith('/start') for c in xbox['chords'])
def normalized(b):
 result=[]
 for source in b['bindings']['/actions/xbox']['sources']:
  path=source['path'].replace('/input/thumbstick','/input/joystick')
  if b['controller_type']=='knuckles':
   path=path.replace('/user/hand/left/input/a','/user/hand/left/input/x').replace('/user/hand/left/input/b','/user/hand/left/input/y')
  result.append((path,source['mode'],json.dumps(source['inputs'],sort_keys=True)))
 return sorted(result)
assert normalized(q)==normalized(k)
for source in k['bindings']['/actions/xbox']['sources']:
 if source['path'].endswith('/input/grip'):assert source['parameters']['force_input']=='force'
for slot in ['left_primary','right_secondary','left_stick','right_stick','left_trigger','right_trigger','left_grip','right_grip']:
 a=next(x for x in devices['quest-touch']['bindings'] if x['slot']==slot)
 b=next(x for x in devices['valve-index']['bindings'] if x['slot']==slot)
 assert a['action']==b['action'],slot
 assert 'Not configured' not in a['action']+b['action']
assert next(x for x in devices['quest-touch']['bindings'] if x['slot']=='left_primary')['action'].startswith('Duck')
assert next(x for x in devices['valve-index']['bindings'] if x['slot']=='left_primary')['control'].startswith('A (left')
assert all(next(x for x in d['bindings'] if x['slot']=='right_secondary')['action'].startswith('Grab') for d in devices.values())
assert all(d.get('context') for d in devices.values())
prepare=(mod/'prepare.ps1').read_text(encoding='utf-8-sig')
for name in ['bindings_oculus_touch.json','bindings_knuckles.json','actions.json']:assert prepare.count(name)>=2,name
view=(root/'client/VrClient.App/Views/MainWindow.axaml').read_text(encoding='utf-8-sig')
assert view.count('<views:ControllerDiagram')==2 or (
 'ItemsSource="{Binding ControllerDevices}"' in view and
 'DiagramAssetUri, Converter={StaticResource ControllerDiagramConverter}' in view and
 'LeftPrimary.Action' in view and 'RightSecondary.Action' in view
)
print('PASS: Quest/Index source-role parity; duck/grab guide; handgrip poses; pause safety; both app diagrams; deployment inputs.')
