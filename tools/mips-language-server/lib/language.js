"use strict";
const { KEYWORDS, TYPES, GLOBALS, ATTRIBUTES } = require("./api");
const TOKEN_TYPES = ["class", "enum", "function", "method", "property", "variable", "type", "parameter", "enumMember", "keyword", "operator", "number", "decorator"];
const primitives = new Set(["void", "bool", "int", "float", "string"]);
const keywords = new Set([...KEYWORDS, "get", "set", "private"]);
function lex(text) {
  const out = [];
  const re = /\/\/[^\r\n]*|\/\*[\s\S]*?(?:\*\/|$)|"(?:\\[\s\S]|[^"\\])*(?:"|$)|[A-Za-z_][A-Za-z0-9_]*|\d+(?:\.\d+)?|==|!=|<=|>=|&&|\|\||[+*/%=!<>-]|[^\s]/g;
  for (const m of text.matchAll(re)) {
    const value = m[0];
    const kind = value.startsWith("//") || value.startsWith("/*") ? "comment" : value[0] === '"' ? "string" : /^[A-Za-z_]/.test(value) ? "identifier" : /^\d/.test(value) ? "number" : /^[+*/%=!<>|&-]/.test(value) ? "operator" : "punctuation";
    out.push({ value, start: m.index, length: value.length, kind });
  }
  return out;
}
function codeOnly(text) {
  const chars = text.split(""); // UTF-16 offsets match VS Code, including surrogate pairs.
  for (const t of lex(text)) if (t.kind === "comment" || t.kind === "string")
    for (let i=t.start; i<t.start+t.length; ++i) if (chars[i] !== "\r" && chars[i] !== "\n") chars[i] = " ";
  return chars.join("");
}
function analyze(text) {
  const tokens = lex(text).filter(t => t.kind !== "comment");
  const types = new Set([...TYPES, ...primitives]);
  const declarations = [], symbols = new Map(), enumTypes = new Set();
  // Recognize field annotation lists, without treating array indices as annotations.
  for (let i = 0; i < tokens.length; ++i) {
    if (tokens[i].value !== "[" || !ATTRIBUTES[tokens[i + 1]?.value.replace(/Attribute$/, "")]) continue;
    const previous = tokens[i - 1]?.value;
    if (![undefined, "{", "}", ";", "]"].includes(previous)) continue;
    let depth = 0;
    for (let j = i + 1; j < tokens.length && tokens[j].value !== "]"; ++j) {
      const t = tokens[j];
      if (t.value === "(") ++depth;
      else if (t.value === ")") --depth;
      else if (depth === 0 && t.kind === "identifier") {
        t.type = "decorator"; t.modifiers = ["defaultLibrary"];
      }
    }
  }
  const mark = (t, type, kind, declaredType="") => {
    if (!t || t.kind !== "identifier") return;
    t.type = type; t.modifiers = ["declaration"];
    const s = { name:t.value, type:declaredType, kind, start:t.start, length:t.length };
    declarations.push(s); symbols.set(t.value, type);
  };
  for (let i=0; i<tokens.length; ++i) {
    const t=tokens[i], next=tokens[i+1];
    if ((t.value === "class" || t.value === "enum") && next?.kind === "identifier") {
      types.add(next.value); mark(next,t.value,t.value,next.value);
      if(t.value === "enum") enumTypes.add(next.value);
    }
  }
  for (let i=0; i<tokens.length; ++i) {
    const t=tokens[i];
    if (t.kind !== "identifier" || t.type || keywords.has(t.value) && t.value !== "var" && !primitives.has(t.value)) continue;
    let j=i+1, declaredType=t.value;
    if(tokens[j]?.value === "[" && tokens[j+1]?.value === "]"){declaredType+="[]";j+=2;}
    const name=tokens[j], after=tokens[j+1];
    if(name?.kind !== "identifier" || !after || !["(","=",";","{",",",")"].includes(after.value)) continue;
    if(after.value === "(") {
      t.type=enumTypes.has(t.value)?"enum":"type"; mark(name,"function","method",declaredType);
      // Formal parameters have their own semantic category.
      let p=j+2;
      while(p<tokens.length && tokens[p].value!==")") {
        let q=p+1, pt=tokens[p].value;
        if(tokens[q]?.value==="[" && tokens[q+1]?.value==="]"){pt+="[]";q+=2;}
        if(tokens[q]?.kind==="identifier") {tokens[p].type="type";mark(tokens[q],"parameter","parameter",pt);p=q+1;}
        else ++p;
        if(tokens[p]?.value===",")++p;
      }
    } else {
      if(t.value!=="var")t.type=enumTypes.has(t.value)?"enum":"type";
      const field=tokens[i-1]?.value==="public";
      if(!name.type)mark(name,field?"property":"variable",field?"field":"local",declaredType);
    }
  }
  // Enum values, including unqualified references, are constants.
  for(let i=0;i<tokens.length;++i) if(tokens[i].value==="enum") {
    let j=i+2; while(j<tokens.length && tokens[j].value!=="{")++j;
    for(++j;j<tokens.length && tokens[j].value!=="}";++j)
      if(tokens[j].kind==="identifier" && ["{",","].includes(tokens[j-1]?.value))mark(tokens[j],"enumMember","enumMember",tokens[i+1].value);
  }
  const result=[];
  for(let i=0;i<tokens.length;++i) {
    const t=tokens[i], prev=tokens[i-1], next=tokens[i+1];
    if(t.kind==="identifier" && !t.type) {
      if(primitives.has(t.value)||types.has(t.value)) t.type=enumTypes.has(t.value)?"enum":"type";
      else if(keywords.has(t.value))t.type="keyword";
      else if(prev?.value===".")t.type=next?.value==="("?"method":enumTypes.has(tokens[i-2]?.value)?"enumMember":"property";
      else if(GLOBALS[t.value] && /^[A-Z]/.test(t.value) && Object.keys(GLOBALS[t.value].members || {}).length){t.type="class";t.modifiers=["static"];}
      else if(next?.value==="(" || (next?.value==="<" && t.value==="GetComponent")) t.type="function";
      else t.type=symbols.get(t.value)||"variable";
      if(prev?.value==="<" && tokens[i-2]?.value==="GetComponent")t.type="type";
      if(prev?.value===":" && tokens[i-2]?.type==="class")t.type="type";
    } else if(t.kind==="number")t.type="number";
    else if(t.kind==="operator")t.type="operator";
    if(primitives.has(t.value)) t.modifiers = ["defaultLibrary"];
    if(t.type)result.push({...t,modifiers:t.modifiers||[]});
  }
  return { tokens:result, declarations };
}
module.exports = { TOKEN_TYPES, lex, codeOnly, analyze };
