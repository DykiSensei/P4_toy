import json
import xml.etree.ElementTree as ET
from pathlib import Path

dest = Path(__file__).resolve().parents[1] / 'docs' / 'ui'
root = ET.parse(dest / 'map-source.osm').getroot()
bbox = [116.3870, 39.9219, 116.3940, 39.9280]
nodes = {n.get('id'): [float(n.get('lon')), float(n.get('lat'))] for n in root.findall('node')}

def segment(a, b):
    x, y = a
    dx, dy = b[0]-x, b[1]-y
    p = [-dx, dx, -dy, dy]
    q = [x-bbox[0], bbox[2]-x, y-bbox[1], bbox[3]-y]
    lo, hi = 0., 1.
    for pi, qi in zip(p, q):
        if pi == 0:
            if qi < 0: return None
        else:
            t = qi/pi
            if pi < 0: lo = max(lo, t)
            else: hi = min(hi, t)
    if lo > hi: return None
    return [[round(x+lo*dx, 7), round(y+lo*dy, 7)], [round(x+hi*dx, 7), round(y+hi*dy, 7)]]

def clipline(coords):
    parts = []
    for a,b in zip(coords, coords[1:]):
        item = segment(a,b)
        if item is None or item[0] == item[1]: continue
        if parts and parts[-1][-1] == item[0]: parts[-1].append(item[1])
        else: parts.append(item)
    return parts

def clippoly(coords):
    pts = coords[:-1]
    for axis, bound, sign in [(0,bbox[0],1),(0,bbox[2],-1),(1,bbox[1],1),(1,bbox[3],-1)]:
        old, pts = pts, []
        if not old: break
        for a,b in zip(old, old[1:]+old[:1]):
            inside_a = sign*(a[axis]-bound) >= 0
            inside_b = sign*(b[axis]-bound) >= 0
            if inside_a: pts.append(a)
            if inside_a != inside_b:
                t = (bound-a[axis])/(b[axis]-a[axis])
                pts.append([a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])])
    if len(pts)<3:return []
    pts = [[round(x,7),round(y,7)] for x,y in pts]
    # D3 spherical geoPath expects clockwise exterior rings for small polygons.
    area = sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(pts,pts[1:]+pts[:1]))
    if area>0:pts.reverse()
    return pts+[pts[0]]

features=[]
for way in root.findall('way'):
    tags = {t.get('k'):t.get('v') for t in way.findall('tag')}
    kind = 'road' if 'highway' in tags else 'green' if tags.get('leisure') in ['park','garden'] or tags.get('natural')=='wood' else None
    if not kind: continue
    refs=[n.get('ref') for n in way.findall('nd')]
    if any(ref not in nodes for ref in refs):continue
    coords=[nodes[ref] for ref in refs]
    props={'kind':kind,'osm_id':int(way.get('id'))}
    for k in ['name','highway','leisure','natural']:
        if k in tags:props[k]=tags[k]
    if kind=='green':
        ring=clippoly(coords)
        if not ring:continue
        geom={'type':'Polygon','coordinates':[ring]}
    else:
        parts=clipline(coords)
        if not parts:continue
        geom={'type':'LineString','coordinates':parts[0]} if len(parts)==1 else {'type':'MultiLineString','coordinates':parts}
    features.append({'type':'Feature','properties':props,'geometry':geom})

for n in root.findall('node'):
    tags={t.get('k'):t.get('v') for t in n.findall('tag')}
    if tags.get('natural')!='peak':continue
    coord=nodes[n.get('id')]
    if bbox[0]<=coord[0]<=bbox[2] and bbox[1]<=coord[1]<=bbox[3]:
        features.append({'type':'Feature','properties':{'kind':'landmark','name':tags.get('name',''),'osm_id':int(n.get('id'))},'geometry':{'type':'Point','coordinates':coord}})

data={'type':'FeatureCollection','name':'北京景山公园 UI 设计示例','bbox':bbox,'attribution':'© OpenStreetMap contributors','license':'ODbL-1.0','source':'https://api.openstreetmap.org/api/0.6/map?bbox=116.3870,39.9219,116.3940,39.9280','features':features}
out=dest/'map-sample.geojson'
out.write_text(json.dumps(data,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
print(json.dumps({'features':len(features),'bytes':out.stat().st_size,'types':{k:sum(f['properties']['kind']==k for f in features) for k in ['green','road','landmark']}},ensure_ascii=False))
