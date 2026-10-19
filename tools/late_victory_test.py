"""Exercise common-race late results using already-running disposable test copies.
The loss is deliberately induced with local arrested+5, not a natural second-place race.
Never starts/stops a game, sends Windows keys or modifies non-test saves.
"""
import sys,time,json,threading,math,argparse
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from ctl import Copy
from race_finish_test import r32,race,racer,state,fl
from result_delay_proxy import ResultDelayProxy
def main():
    parser=argparse.ArgumentParser(description='Disposable-copy test of native late-result handling; never launches or closes game processes.')
    parser.add_argument('--case',choices=('win','loss','timeout'),default='win')
    parser.add_argument('--host-port',type=int,default=45803)
    parser.add_argument('--partner-port',type=int,default=45804)
    args=parser.parse_args()
    h,p=Copy(args.host_port,timeout=8),Copy(args.partner_port,timeout=8)
    root=Path(__file__).resolve().parents[1]/'out'/'live-isolated'/'late-tests'/f'{args.case}-{time.time_ns()}'
    root.mkdir(parents=True,exist_ok=True)
    log=(root/f'late-{args.case}-proxy.log').open('w',encoding='utf-8')
    proxy=ResultDelayProxy(45520,45521,20 if args.case=='timeout' else 2,lambda s:(log.write(s+'\n'),log.flush()))
    t=threading.Thread(target=proxy.run,args=(420,),daemon=True);t.start()
    evidence={'case':args.case}
    def localstats(c):
     data=r32(c,r32(c,0x557F0C)+0xC)
     return {'wins':r32(c,data+0xA78)&0xFFFF,'money':r32(c,data+0xACC)}
    def read(c):
     r=race(c)
     return {'state':c.command('state'),'local':racer(c,0),'humans_finished':r32(c,r+0x1250),'reward_guard':r32(c,r+0xC),'best_lap':fl(c,r+0x98+0x28,1)[0],'stats':localstats(c)}
    def placecp(c,cp):
     r=race(c);n=r32(c,r+0x24);assert 0<=cp<n
     xyz=fl(c,r32(c,r+0x28)+cp*0x120+0xFC,3);assert all(math.isfinite(x) for x in xyz)
     c.command(f'place {xyz[0]} {xyz[1]+0.5} {xyz[2]}')
    try:
     print(p.command('coop join 127.0.0.1 45521'),flush=True);time.sleep(3)
     for c in (h,p):assert 'CONNECTED' in c.command('state')
     evidence['baseline']=[localstats(c) for c in (h,p)]
     # First city0 race; remove roaming-hookman and car-class filters only in the disposable save.
     index=None
     data=r32(h,r32(h,0x557F0C)+0xC);begin,end=r32(h,data+0xA5C),r32(h,data+0xA60)
     for idx,a in enumerate(range(begin,end,0x4C)):
      w=r32(h,a+8)
      if w&0xFF==0:
       h.command(f'poke {a+8:08X} {(w&0xFF0000FF)|0x00FFFF00:08X}');index=idx;break
     if index is None:raise RuntimeError('No open city0 race in disposable host save')
     print(h.command(f'gorace {index}'),flush=True)
     for _ in range(100):
      ss=[c.command('state') for c in (h,p)]
      if all('cruise=0' in s and 'race_state=5' in s for s in ss):break
      time.sleep(.5)
     else:raise RuntimeError('Common start failed '+repr(ss))
     for c in (h,p):
      assert r32(c,race(c))==0x46B2D8
      assert 'gate=1 ' in c.command('state'),c.command('state')
     print('both gate armed; prepare last CP',flush=True)
     for step in range(400):
      ready=[]
      for c in (h,p):
       me=racer(c,0);n=r32(c,race(c)+0x24)
       assert not me['finished']
       done=me['laps_left']==1 and me['next_cp']==n-1;ready.append(done)
       if not done:placecp(c,me['next_cp'])
      if all(ready):break
      time.sleep(.23)
     else:raise RuntimeError('Last CP prep timed out')
     evidence['prepared']=[read(c) for c in (h,p)]
     # Induce native local loss without changing DNF or participation, counters or AI finish bits.
     a=race(h)+0x98+4;word=r32(h,a);h.command(f'poke {a:08X} {word|0x100:08X}')
     if args.case=='loss':
      a=race(p)+0x98+4;word=r32(p,a);p.command(f'poke {a:08X} {word|0x100:08X}')
     mark=time.monotonic();placecp(p,racer(p,0)['next_cp'])
     for _ in range(60):
      if proxy.stats['queued']>0:break
      time.sleep(.02)
     else:raise RuntimeError('No peer result queued')
     placecp(h,racer(h,0)['next_cp'])
     time.sleep(.25)
     evidence['during_delay']=[read(c) for c in (h,p)]
     print('DURING',json.dumps(evidence['during_delay']),flush=True)
     assert 'race_state=6' in evidence['during_delay'][0]['state']
     assert 'awaiting_peer_terminal' in evidence['during_delay'][0]['state']
     assert evidence['during_delay'][0]['humans_finished']==1
     assert evidence['during_delay'][0]['reward_guard']&0xFFFF00==0
     if args.case!='loss':
      assert 'ai=experimental' in evidence['during_delay'][1]['state'], 'peer result released AI while host still racing'
     time.sleep(16 if args.case=='timeout' else 3)
     evidence['after_delay']=[read(c) for c in (h,p)]
     evidence['proxy_stats']=dict(proxy.stats)
     print('AFTER',json.dumps(evidence['after_delay']),flush=True)
     for i,(before,after) in enumerate(zip(evidence['baseline'],evidence['after_delay'])):
      should_win=args.case=='win' or (args.case=='timeout' and i==1)
      assert after['local']['won']==int(should_win)
      assert after['humans_finished']==2
      assert after['stats']['wins']==before['wins']+int(should_win)
      assert after['stats']['money']>before['money'] if should_win else after['stats']['money']==before['money']
      assert after['best_lap']>=evidence['during_delay'][i]['best_lap']
     # Let the 20s terminal packet arrive AFTER the host's 15s local-result decision.
     if args.case=='timeout':time.sleep(5)
     # Repeated/late packets must not count again; check final ranks in both worlds too.
     from ai_test import ranking
     samples=[]
     for _ in range(3):
      a,b=ranking(h),ranking(p);samples.append([a,b]);assert a==b,(a,b)
      time.sleep(1)
     evidence['AI_final_ranks']=samples
     evidence['stable']=[read(c) for c in (h,p)]
     for a,b in zip(evidence['after_delay'],evidence['stable']):assert a['stats']==b['stats']
     for c in (h,p):c.command(f'shot {root.resolve()}\\late-{args.case}-{c.port}.bmp')
     evidence['pass']=True
     print('PASS late-result case',args.case,'native result once; AI final ranks equal; evidence',root,flush=True)
    finally:
     (root/f'late-{args.case}-evidence.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
     proxy.stopped.set();t.join(3);log.close()


if __name__ == "__main__":
    main()
