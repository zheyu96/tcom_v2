import csv,collections,sys
def load(f): return {(r['sweep'],r['parameter_value'],r['instance'],r['algorithm']):r for r in csv.DictReader(open(f))}
B=load(sys.argv[1]); N=load(sys.argv[2])
agg=collections.defaultdict(lambda:[0.0]*8)
for k in B:
    x,y=B[k],N[k];a=agg[(k[0],k[3])]
    for i,f in enumerate(['run_seconds','fidelity_gain','succ_request_cnt','dp_paths']):
        a[2*i]+=float(x[f]);a[2*i+1]+=float(y[f])
    a.append(max(float(y['peak_labels']),0))
for k,a in sorted(agg.items()):
    print(f"{k[0]:18s} {k[1]:4s} | run {a[0]:6.2f}->{a[1]:5.2f} ({a[0]/a[1]:4.1f}x) | fid {a[2]:8.2f}->{a[3]:8.2f} ({(a[3]-a[2])/a[2]*100:+.2f}%) | succ {a[4]:.1f}->{a[5]:.1f} | dp_paths {a[6]:.0f}->{a[7]:.0f} | peakL {max(a[8:]):.0f}")
