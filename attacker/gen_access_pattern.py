import argparse, random

MAPPING_FUNCS = {
    'channel': [8],
    'rank': [14],
    'bg1': [21],
    'bg0': [20],
    'ba1': [19],
    'ba0': [6]
}

CL_SIZE = 64

def get_max_bit(funcs):
    return max(max(x) for x in funcs.values() if x)

def apply_func(f, addr):
    res = False
    for k in f:
        res ^= (addr & (1 << k)) != 0
    return int(res)

def compute_mapping(funcs, addr):
    res = {}
    res['channel'] = apply_func(funcs['channel'], addr)
    res['rank'] = apply_func(funcs['rank'], addr)
    res['bg'] = ((apply_func(funcs['bg1'], addr) << 1) | apply_func(funcs['bg0'], addr))
    res['bank'] = ((apply_func(funcs['ba1'], addr) << 1) | apply_func(funcs['ba0'], addr))
    return res

# Format of buckets: (channel, rank, bg, bank) -> [list of cacheline addresses]
def construct_buckets(funcs, slice_size):
    res = {}
    for addr in range(0, slice_size, CL_SIZE):
        m = compute_mapping(funcs, addr)
        k = (m['channel'], m['rank'], m['bg'], m['bank'])
        if not k in res:
            res[k] = []
        res[k].append(addr)
    return res


if __name__ == "__main__":
    
    parser = argparse.ArgumentParser()
    
    parser.add_argument(
        "--num_requests", 
        type=int, 
        required=True, 
        help="number of requests."
    )
    parser.add_argument(
        "--log_wss", 
        type=int, 
        required=True, 
        help="log base 2 of working set size"
    )
    parser.add_argument(
        "--out_file", 
        type=str, 
        required=True, 
        help="output file path"
    )

    args = parser.parse_args()

    # Minimum address space size to cover all banks
    slice_size = (1 << (get_max_bit(MAPPING_FUNCS) + 1))
    wss = (1 << args.log_wss)
    num_slices = int(wss/slice_size)
    print(f'Slice size: {slice_size}, number of slices: {num_slices}')

    # Bucket addresses for a slice
    buckets = construct_buckets(MAPPING_FUNCS, slice_size)
    bank0_addrs = buckets[(0, 0, 0, 0)]

    with open(args.out_file, 'w') as f:
        for i in range(args.num_requests):
            # Pick random slice
            slice_idx = random.randrange(0, num_slices)
            # Pick random cacheline in slice
            cl_addr = bank0_addrs[random.randrange(0, len(bank0_addrs))]
            # Compute address
            addr = slice_idx * slice_size + cl_addr
            f.write('%d\n'%(addr))

