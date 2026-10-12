const assert = require('assert/strict');
const fs = require('fs');
const path = require('path');
const root=path.resolve(__dirname,'..');
const {lex, analyze, codeOnly}=require('../tools/mips-language-server/lib/language');
const api=require('../tools/mips-language-server/lib/api');
const analyzer=require('../tools/mips-language-server/lib/analyzer');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const lexer=read('src/mips/Lexer.cpp');
for(const m of lexer.matchAll(/\{ "(\w+)", TokenType::Kw/g))assert(api.KEYWORDS.includes(m[1]),`Missing lexer keyword ${m[1]}`);
const compiler=read('src/mips/Compiler.cpp');
for(const m of compiler.matchAll(/IsMemberChain\(expr, "(\w+)", "(\w+)"\)/g))assert(api.GLOBALS[m[1]]?.members[m[2]],`Missing property ${m[1]}.${m[2]}`);
for(const m of compiler.matchAll(/targetName == "(\w+)" && methodName == "(\w+)"/g))assert(api.GLOBALS[m[1]]?.members[m[2]],`Missing API ${m[1]}.${m[2]}`);
for(const [begin,end,owner] of [['if (targetName == "Mathf")','if (targetName == "Animator")','Mathf'],['if (targetName == "Vector3")','if (targetName == "Mathf")','Vector3'],['if (targetName == "Save")','return false','Save'],['auto tryAnimatorMethods','auto tryAudioSourceMethods','Animator'],['auto tryAudioSourceMethods','if (call.callee->kind','AudioSource']]){
 const a=compiler.indexOf(begin),b=compiler.indexOf(end,a+begin.length);if(a<0)continue;
 for(const m of compiler.slice(a,b<0?undefined:b).matchAll(/methodName == "(\w+)"/g))assert(api.GLOBALS[owner].members[m[1]],`Missing ${owner}.${m[1]}`);
}
for (const method of ['GetButton','GetButtonDown','GetButtonUp','GetAxis']) {
 assert(api.GLOBALS.Input.members[method],`Missing PS1 input API ${method}`);
 const code=`class Pad : MipsBehaviour { void Update() { Input.${method}("Cross"); } }`;
 assert(analyze(code).tokens.some(t=>t.value===method && t.type==='method'),`Uncolored PS1 input API ${method}`);
}
const sample='// 😀 ignored class Fake {}\r\nenum Mode { Idle, Run = 2 };\r\nclass Foo : MipsBehaviour { public Camera follow; public float Speed { get; private set; }; public int[] values = [1,2]; public void Move(float delta, Vector3 step) { float velocity = delta * 2.0; var anim = GetComponent<Animator>(); anim.SetFloat("class Bogus {}",velocity); if (velocity <= 2 && true) { velocity = velocity + 1; } } }';
const analysis=analyze(sample);
assert(!analysis.declarations.some(s=>['Fake','Bogus'].includes(s.name)));
for(const t of lex(sample).filter(t=>['identifier','number','operator'].includes(t.kind)))assert(analysis.tokens.some(s=>s.start===t.start&&s.length===t.length),`Uncolored ${t.value} at ${t.start}`);
const at=value=>analysis.tokens.filter(t=>t.value===value);
assert(at('delta').every(t=>t.type==='parameter'));assert(at('Idle')[0].type==='enumMember');assert(at('follow')[0].type==='property');assert(at('SetFloat')[0].type==='method');assert(at('velocity').every(t=>t.type==='variable'));assert(at('GetComponent')[0].type==='function');assert(at('Animator')[0].type==='type');
assert.equal(codeOnly(sample).length,sample.length);
for(let offset=0;offset<sample.length;++offset){if(sample[offset]==='\r')continue;assert.equal(analyzer.offsetAt(sample,analyzer.positionAt(sample,offset)),offset,'CRLF/UTF16 coordinate mismatch');}
const completionText='class Foo : MipsBehaviour { public void Move(Camera cam, Vector3[] vectors) { var anim = GetComponent<Animator>(); anim. } }';
const pos=analyzer.positionAt(completionText,completionText.indexOf('anim. ')+5);
assert(analyzer.getCompletions(completionText,pos).some(c=>c.label==='GetNormalizedTime'));
const hoverText='class Foo : MipsBehaviour { void Update() { Mathf.Clamp(1,0,2); } }';
assert(analyzer.getHover(hoverText,analyzer.positionAt(hoverText,hoverText.indexOf('Clamp')+2)).contents.value.includes('float Clamp'));
for (const [name,info] of Object.entries(api.ATTRIBUTES)) {
 const text=`class Foo : MipsBehaviour {\n    [${name}] public float field;\n}`;
 assert(analyze(text).tokens.find(t=>t.value===name)?.type==='decorator',`Uncolored attribute ${name}`);
 assert(analyzer.getHover(text,analyzer.positionAt(text,text.indexOf(name)+1)).contents.value.includes(info.signature),`Missing attribute hover ${name}`);
 for(const prefix of ['[','[Header("Title"), ','[Space] [']) {
  const typing=`class Foo : MipsBehaviour {\n    ${prefix}`;
  assert(analyzer.getCompletions(typing,analyzer.positionAt(typing,typing.length)).some(c=>c.label===name&&c.insertTextFormat===2),`Missing attribute completion ${name}`);
 }
}
const indexed='class Foo : MipsBehaviour { public void Move() { float result = values[Header]; } }';
assert(analyze(indexed).tokens.find(t=>t.value==='Header')?.type!=='decorator','Array index mistaken for attribute');
for(const file of ['tests/mips/SilentHillController.mips','examples/MipsLanguageV03.mips','tests/mips/EditorLanguageCoverage.mips']){
 const s=read(file),a=analyze(s);for(const t of lex(s).filter(t=>['identifier','number','operator'].includes(t.kind)))assert(a.tokens.some(x=>x.start===t.start),`${file}: ${t.value} uncolored`);
}
(async()=>{
 const moduleRoot=path.resolve(process.argv[2]||path.join(root,'tools/vscode-mips/node_modules'));
 const {Registry,parseRawGrammar}=require(path.join(moduleRoot,'vscode-textmate'));
 const onig=require(path.join(moduleRoot,'vscode-oniguruma'));
 const wasm=fs.readFileSync(path.join(moduleRoot,'vscode-oniguruma/release/onig.wasm'));await onig.loadWASM(wasm.buffer.slice(wasm.byteOffset,wasm.byteOffset+wasm.byteLength));
 const registry=new Registry({onigLib:Promise.resolve({createOnigScanner:p=>new onig.OnigScanner(p),createOnigString:s=>new onig.OnigString(s)}),loadGrammar:async()=>parseRawGrammar(read('tools/vscode-mips/syntaxes/mips.tmLanguage.json'),'mips.tmLanguage.json')});
 const grammar=await registry.loadGrammar('source.mips');
 for (const name of Object.keys(api.ATTRIBUTES)) {
  const line=`    [${name}]`;
  assert(grammar.tokenizeLine(line).tokens.find(t=>line.slice(t.startIndex,t.endIndex)===name)?.scopes.includes('support.type.attribute.mips'),`TextMate attribute ${name} uncolored`);
 }
 for(const file of ['tests/mips/SilentHillController.mips','examples/MipsLanguageV03.mips','tests/mips/EditorLanguageCoverage.mips']){
  let stack;for(const line of read(file).split(/\r?\n/)){const result=grammar.tokenizeLine(line,stack);stack=result.ruleStack;for(const token of result.tokens)if(line.slice(token.startIndex,token.endIndex).trim())assert(token.scopes.length>1,`${file}: unscoped ${line.slice(token.startIndex,token.endIndex)}`);}
 }
 const line='else if (Camera.forwardX != 0.0) { float speed = Mathf.Sin(1.0); }';
 const out=grammar.tokenizeLine(line).tokens;for(const word of ['else','if'])assert(out.find(t=>line.slice(t.startIndex,t.endIndex)===word).scopes.some(s=>s==='keyword.control.mips'),`TextMate misreads ${word} as declaration`);
 registry.dispose();console.log('Mips# lexer/API parity, semantic coverage, CRLF/emoji positions, completion/hover and real TextMate grammar tests passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
