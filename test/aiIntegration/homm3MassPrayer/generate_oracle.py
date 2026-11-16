from pathlib import Path
import hashlib,json,sys
p=Path(sys.argv[1]);s=p.read_text();out=Path(__file__).parent
def extract(tag):
 a=s.index(tag);b=s.index('{',a)+1;n=1
 while n:n+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
tags=['long type_AI_spellcaster::getDispelValue(', 'void type_AI_spellcaster::considerEnchantment(']
parts=[extract(x) for x in tags]
a=s.index('case SPELL_DISPEL: {',s.index('void type_AI_spellcaster::considerSpell('));b=s.index('    case SPELL_EARTHQUAKE:',a)
fragment=s[a:b];(out/'dispatch.pinned.inc').write_text(fragment)
(out/'bodies.pinned.inc').write_text('\n\n'.join(parts)+'\n')
(out/'manifest.json').write_text(json.dumps({'pin':'e12906accf96c2662b1abadbc49ddc111991bf21','file_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bodies':[{'signature':t,'sha256':hashlib.sha256(x.encode()).hexdigest()} for t,x in zip(tags,parts)],'dispel_dispatch_sha256':hashlib.sha256(fragment.encode()).hexdigest()},indent=2)+'\n')
