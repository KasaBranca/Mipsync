// Usage: node tools/Verify-Ps1Disc.cjs <game.cue> <licensej.dat> [report.json]
// Read-only verification of the actual burned image, including every EDC/ECC.
const fs=require('fs'), path=require('path'), crypto=require('crypto');
const [cuePath,licensePath,reportPath]=process.argv.slice(2);
if(!cuePath||!licensePath) throw Error('Expected cue and standard system-area paths');
const check=(ok,message)=>{if(!ok) throw Error(message);};
const cue=fs.readFileSync(cuePath,'utf8');
check(/TRACK\s+01\s+MODE2\/2352/i.test(cue),'Track 1 must be MODE2/2352');
check(/INDEX\s+01\s+00:00:00/.test(cue),'Track 1 must start at image sector zero');
const file=/FILE\s+"([^"]+)"\s+BINARY/i.exec(cue);
check(file,'Missing BIN file in CUE');
const binPath=path.resolve(path.dirname(cuePath),file[1]);
const bin=fs.readFileSync(binPath), license=fs.readFileSync(licensePath);
check(bin.length%2352===0,'Incomplete raw CD sector');
check([12*2336,16*2336].includes(license.length),'Invalid standard system area');
const data=i=>bin.subarray(i*2352+24,i*2352+24+2048);
const edc=new Uint32Array(256), forward=new Uint8Array(256), backward=new Uint8Array(256);
for(let i=0;i<256;i++) {let v=i;for(let b=0;b<8;b++)v=(v>>>1)^((v&1)?0xd8018001:0);edc[i]=v;let j=i<<1;if(j&256)j^=0x11d;forward[i]=j;backward[i^j]=i;}
function parity(src,majorCount,minorCount,majorMult,minorInc){const size=majorCount*minorCount,out=Buffer.alloc(majorCount*2);for(let major=0;major<majorCount;major++){let index=(major>>1)*majorMult+(major&1),a=0,b=0;for(let minor=0;minor<minorCount;minor++){const t=src[index];index=(index+minorInc)%size;a^=t;b^=t;a=forward[a];}a=backward[forward[a]^b];out[major]=a;out[major+majorCount]=a^b;}return out;}
let form1=0,form2=0;
for(let i=0;i<bin.length/2352;i++) {
 const sector=bin.subarray(i*2352,(i+1)*2352);
 check(sector.subarray(0,12).equals(Buffer.from('00ffffffffffffffffffff00','hex'))&&sector[15]===2,`Invalid Mode2 sector ${i}`);
 check(sector.subarray(16,20).equals(sector.subarray(20,24)),`XA subheader mismatch ${i}`);
 const isForm2=!!(sector[18]&0x20),end=isForm2?2348:2072;
 let crc=0;for(let j=16;j<end;j++)crc=(crc>>>8)^edc[(crc^sector[j])&255];
 check(sector.readUInt32LE(end)===(crc>>>0),`EDC mismatch ${i}`);
 if(isForm2){++form2;continue;}
 ++form1;
 const zeroHeader=Buffer.from(sector.subarray(12));zeroHeader.fill(0,0,4);
 check(parity(zeroHeader,86,24,2,86).equals(sector.subarray(2076,2248)),`ECC P mismatch ${i}`);
 check(parity(zeroHeader,52,43,86,88).equals(sector.subarray(2248,2352)),`ECC Q mismatch ${i}`);
}
for(let i=0;i<license.length/2336;i++)check(data(i).equals(license.subarray(i*2336+8,i*2336+8+2048)),`Changed system-area payload ${i}`);
const region=data(4).subarray(0,128).toString('ascii').replace(/\0/g,'').trim();
if(path.basename(licensePath).toLowerCase()==='licensej.dat')check(region.startsWith('Licensed  by          Sony Computer Entertainment Inc.'),'Missing Japan BIOS license identification');
const logo=Buffer.concat(Array.from({length:7},(_,i)=>data(5+i))).subarray(0,0x3278);
check(logo.readUInt32LE(0)===0x41,'Missing PlayStation system-area TMD');
const pvd=data(16);check(pvd[0]===1&&pvd.subarray(1,6).toString()==='CD001','Missing ISO9660 primary volume descriptor');
check(pvd.readUInt32LE(80)<=bin.length/2352,'ISO volume exceeds image');
const directoryLba=pvd.readUInt32LE(158), directorySize=pvd.readUInt32LE(166);
const directory=Buffer.concat(Array.from({length:Math.ceil(directorySize/2048)},(_,i)=>data(directoryLba+i)));
const files={};for(let offset=0;offset<directorySize;) {const len=directory[offset];if(!len){offset=(Math.floor(offset/2048)+1)*2048;continue;}const rec=directory.subarray(offset,offset+len);const name=rec.subarray(33,33+rec[32]).toString('ascii');files[name]={lba:rec.readUInt32LE(2),bytes:rec.readUInt32LE(10)};offset+=len;}
check(files['SYSTEM.CNF;1']&&files['PSX.EXE;1'],'Missing SYSTEM.CNF or PSX.EXE');
const cnf=data(files['SYSTEM.CNF;1'].lba).subarray(0,files['SYSTEM.CNF;1'].bytes).toString();
check(/BOOT\s*=\s*cdrom:\\PSX\.EXE;1/i.test(cnf),'SYSTEM.CNF boot path mismatch');
const exe=data(files['PSX.EXE;1'].lba), load=exe.readUInt32LE(0x18), bytes=exe.readUInt32LE(0x1c);
check(exe.subarray(0,8).toString()==='PS-X EXE','Invalid PS-X EXE header');
check(load>=0x80010000&&load+bytes<=0x80200000,'Executable exceeds PS1 2 MiB RAM');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const result={cue:path.resolve(cuePath),bin:binPath,sha256:sha(bin),sectors:bin.length/2352,mode:'MODE2/2352',form1,form2,allEdcEccValid:true,systemAreaPayloadPreserved:true,systemAreaSha256:sha(license),region:region.split('\n')[0],stockTmdSha256:sha(logo),systemCnf:cnf.trim(),executable:{load:'0x'+load.toString(16),bytes,end:'0x'+(load+bytes).toString(16)}};
if(reportPath)fs.writeFileSync(reportPath,JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result,null,2));
