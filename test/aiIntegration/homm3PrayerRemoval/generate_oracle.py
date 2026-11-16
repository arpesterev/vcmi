from pathlib import Path
import hashlib,json,sys
p=Path(sys.argv[1]);s=p.read_text();out=Path(__file__).parent
def extract(tag):
 a=s.index(tag);b=s.index('{',a)+1;n=1
 while n:n+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
tags=['long type_AI_spellcaster::getCancelValue(', 'long type_AI_spellcaster::getPrayerValue(', 'long type_AI_spellcaster::getDefenseSkillValue(', 'long type_AI_spellcaster::getDefenseBoostValue(', 'long type_AI_spellcaster::getAttackSkillValue(', 'long type_AI_spellcaster::getAttackBoostValue(const army* ourArmy,\n    const army* enemy, long oldDamage,', 'long type_AI_spellcaster::getAttackBoostValue(const army* ourArmy,\n    const army* enemy, long duration,','long type_AI_spellcaster::getSpeedValue(', 'double type_AI_spellcaster::getDuration(']
parts=[extract(x) for x in tags];(out/'bodies.pinned.inc').write_text('\n\n'.join(parts)+'\n');(out/'manifest.json').write_text(json.dumps({'pin':'e12906accf96c2662b1abadbc49ddc111991bf21','file_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bodies':[{'signature':t,'sha256':hashlib.sha256(b.encode()).hexdigest()} for t,b in zip(tags,parts)]},indent=2)+'\n')
