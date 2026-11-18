import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'variant_a'))
from main import MemoryManager, OPS2, run
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

class Buddy:
    def __init__(self,total=64):
        assert total>0 and total & (total-1)==0
        self.total=total
        self.free_lists={2**i:set() for i in range(total.bit_length())}
        self.free_lists[total].add(0)
        self.active={}
        self.failed=0
        self.external_samples=[]
        self.internal_samples=[]
    def allocate(self,name,size):
        if size<=0 or name in self.active: raise ValueError('Invalid allocation')
        target=1 << (size-1).bit_length()
        available=next((s for s in sorted(self.free_lists) if s>=target and self.free_lists[s]),None)
        if available is None:
            self.failed+=1
            return False
        start=min(self.free_lists[available]); self.free_lists[available].remove(start)
        while available>target:
            available//=2
            self.free_lists[available].add(start+available)
            print(f'  split: [{start},{start+2*available}) -> [{start},{start+available}) + [{start+available},{start+2*available})')
        self.active[name]=(start,target,size)
        return True
    def free(self,name):
        if name not in self.active:return False
        start,size,_=self.active.pop(name)
        while size<self.total and (start ^ size) in self.free_lists[size]:
            buddy=start^size
            self.free_lists[size].remove(buddy)
            print(f'  merge: [{start},{start+size}) + [{buddy},{buddy+size})')
            start=min(start,buddy);size*=2
        self.free_lists[size].add(start)
        return True
    def fragmentation(self):
        free=sum(s*len(starts) for s,starts in self.free_lists.items())
        largest=max((s for s,starts in self.free_lists.items() if starts),default=0)
        return (free-largest)/free if free else 0
    def internal(self):
        reserved=sum(size for _,size,_ in self.active.values())
        waste=sum(size-requested for _,size,requested in self.active.values())
        return waste/reserved if reserved else 0
    def sample(self):
        self.external_samples.append(self.fragmentation())
        self.internal_samples.append(self.internal())

if __name__=='__main__':
    print('OPS2 demo: no coalescing versus coalescing')
    for merge in (False,True):
        m=run(MemoryManager(merge=merge),OPS2)
        print('merge:',merge,'refusals:',m.failed,'map:',m.map())
    results=[]
    for strategy in ('first','best','worst'):
        m=run(MemoryManager(strategy=strategy),OPS2)
        results.append((strategy,m.failed,sum(m.external_samples)/len(m.external_samples),0.0))
    buddy=Buddy()
    for action,name,size in OPS2:
        ok=buddy.allocate(name,size) if action=='alloc' else buddy.free(name)
        print('buddy:',action,name,'OK' if ok else 'REFUSED')
        buddy.sample()
    results.append(('buddy',buddy.failed,sum(buddy.external_samples)/len(buddy.external_samples),sum(buddy.internal_samples)/len(buddy.internal_samples)))
    out=Path(__file__).resolve().parents[1]/'results';out.mkdir(exist_ok=True)
    import csv
    with (out/'comparison.csv').open('w',newline='') as f:
        w=csv.writer(f);w.writerow(['strategy','refusals','avg_external_fraction','avg_internal_fraction']);w.writerows(results)
    print('\nstrategy | refusals | avg external | avg internal')
    for name,failed,external,internal in results:
        print(f'{name:7} | {failed:8} | {external:12.1%} | {internal:12.1%}')
    fig,axes=plt.subplots(1,3,figsize=(12,4))
    for ax,index,title in zip(axes,(1,2,3),('Refusals','Avg external fragmentation','Avg internal fragmentation')):
        ax.bar([r[0] for r in results],[r[index] for r in results]);ax.set_title(title)
        ax.tick_params(axis='x',rotation=20)
    fig.tight_layout();fig.savefig(out/'comparison.png',dpi=160)
    print('Saved results/comparison.csv and results/comparison.png')
