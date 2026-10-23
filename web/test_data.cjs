// Meaningful regression checks for validity/freshness and graph gaps. Node >=18.
const assert = require('node:assert/strict');
const {finiteNumber, parseFeed, parseStatus, normalizeFeeds, freshness, segments, csvCell} = require('./plugin.js');
const raw = (change = {}) => ({created_at:'2026-10-23T15:00:00Z',entry_id:1,field1:'220',field2:'1',field3:'198',field4:'1.25',field5:'60',field6:'0.9',field7:'0.2',field8:'0.5',status:'ver=2;state=RUN;e=1;v=1;ea=200;va=100;alarm=0;ref=1;run=1;ot=1;rc=10;ct=0;vx=0.1;vy=0.12;vz=0.14;cf=2.5;fs=500;ns=500;mi=0;rj=0;rp=0;dp=0;vr=1;boot=3;seq=4;vf=1',...change});
for (const input of [null,undefined,'',' ','na','NaN','Infinity',[],{},true,'0x10','12evil']) assert.equal(finiteNumber(input),null);
assert.equal(finiteNumber('0'),0);assert.equal(finiteNumber('1.2e-3'),0.0012);
let sample = parseFeed(raw());assert.equal(sample.energyValid,true);assert.equal(sample.vibrationValid,true);
assert.equal(parseFeed(raw({field1:null})).values[0],null);
assert.equal(parseFeed(raw({field1:null})).values[2],null); // all electrical fields share validity
assert.equal(parseFeed(raw({field7:null})).vibrationValid,false);
assert.equal(parseFeed(raw({field8:'0.1'})).vibrationValid,false); // peak cannot be below RMS
assert.equal(parseFeed(raw({field6:'1.2'})).energyValid,false);
sample=parseFeed(raw({field2:'0',field3:'0',field6:'0'}));assert.equal(sample.values[2],0); // zero is a real reading
sample=parseFeed(raw({status:raw().status.replace('e=1','e=0')}));assert.equal(sample.energyValid,false);assert.equal(sample.values[2],null);assert.equal(sample.vibrationValid,true);
sample=parseFeed(raw({status:raw().status.replace('v=1','v=0')}));assert.equal(sample.values[6],null);assert.equal(sample.energyValid,true);
assert.equal(parseFeed(raw({status:''})).metaValid,false);
assert.equal(parseFeed(raw({status:raw().status.replace('ver=2','ver=1')})).metaValid,false);
assert.equal(parseFeed(raw({created_at:'wrong'})),null);
assert.equal(parseStatus('__proto__=oops;state=<script>alert(1)</script>').__proto__,undefined);
assert.equal(parseFeed(raw({status:raw().status.replace('RUN','<script>')})).metaValid,false);
sample=parseFeed(raw());let f=freshness(sample,sample.time+64000);assert.equal(f.fresh,true);assert.equal(f.energyAge,64200);
assert.equal(freshness(sample,sample.time+65001).fresh,false);
assert.equal(freshness(sample,sample.time-16000).future,true);
assert.equal(freshness(parseFeed(raw({status:raw().status.replace('ea=200','ea=-1')})),sample.time+20000).energyAge,null);
const a=parseFeed(raw());const b=parseFeed(raw({created_at:'2026-10-23T15:00:20Z',entry_id:2}));const c=parseFeed(raw({created_at:'2026-10-23T15:00:40Z',field7:null,entry_id:3}));const d=parseFeed(raw({created_at:'2026-10-23T15:01:00Z',entry_id:4}));
assert.equal(segments([a,b,c,d],6).length,2);assert.equal(segments([a,b,d],6).length,2);assert.equal(segments([a,b,c,d],2).length,1);
assert.equal(normalizeFeeds([raw({entry_id:2}),raw({created_at:'2026-10-23T14:00:00Z',entry_id:1})])[0].entryId,1);
assert.equal(csvCell('=cmd()'),'"\'=cmd()"');
console.log('E-TWIN web: validity, null/zero, status version, stale/future clock, missing nodes, graph gaps and CSV tests passed.');

assert.equal(parseFeed(raw({status:raw().status.replace('run=1','run=5')})).metaValid,false);
assert.equal(parseFeed(raw({status:raw().status.replace('ot=1','ot=7')})).metaValid,false);
