import csv
import sys

MAPPING_FUNCS = {
    'channel': [8],
    'rank': [14],
    'bg1': [21],
    'bg0': [20],
    'ba1': [19],
    'ba0': [6]
}

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

class PhyAddr:
    def __init__(self, addr, channel, rank, bg, bank):
        self.addr = addr        # Physical address
        self.channel = channel  # Channel identifier
        self.rank = rank        # Rank identifier
        self.bg = bg            # Bank group identifier
        self.bank = bank        # Bank identifier

    def __repr__(self):
        return (f"PhyAddr(addr={self.addr}, channel={self.channel}, "
                f"rank={self.rank}, bg={self.bg}, bank={self.bank})")


def parse_csv_to_dict(file_path):
    phy_addr_dict = {}

    with open(file_path, 'r') as file:
        reader = csv.reader(file)
        for row in reader:
            # Extract and parse each field
            addr = int(row[0], 16)  # Address in hex, convert to int
            channel = int(row[1])   # Channel as integer
            rank = int(row[2])      # Rank as integer
            bg = int(row[3])        # Bank group as integer
            bank = int(row[4])      # Bank as integer

            # Create an instance of PhyAddr
            phy_addr = PhyAddr(addr, channel, rank, bg, bank)

            # Add to the dictionary with addr as key
            phy_addr_dict[addr] = phy_addr

    return phy_addr_dict

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 compute_functions.py <file_path>")
        sys.exit(1)

    file_path = sys.argv[1]
    phy_addr_dict = parse_csv_to_dict(file_path)
    print('Parsed dataset with %d mappings' % (len(phy_addr_dict)))

    # Check mappings for power-of-two addresses
    for i in range(6, 30):
        addr = (1 << i)
        if not addr in phy_addr_dict:
            raise Exception(f'Missing address mapping for {hex(addr)}')
        print(f"{hex(addr)}: {phy_addr_dict[addr]}")

    print('Mapping for power-of-two addresses found')

    # Test mapping function
    channel_hits = 0
    rank_hits = 0
    bg_hits = 0
    bank_hits = 0
    overall_hits = 0
    for addr, phy_addr in phy_addr_dict.items():
        hit = True
        
        m = compute_mapping(MAPPING_FUNCS, addr)
        if m['channel'] == phy_addr.channel:
            channel_hits += 1
        else:
            hit = False
        
        if m['rank'] == phy_addr.rank:
            rank_hits += 1
        else:
            hit = False

        if m['bg'] == phy_addr.bg:
            bg_hits += 1
        else:
            hit = False

        if m['bank'] == phy_addr.bank:
            bank_hits += 1
        else:
            hit = False

        if hit:
            overall_hits += 1

    total = len(phy_addr_dict)
    print(f'Overall accuracy: {overall_hits/total}')
    print(f'channel accuracy: {channel_hits/total}')
    print(f'rank accuracy: {rank_hits/total}')
    print(f'bg accuracy: {bg_hits/total}')
    print(f'bank accuracy: {bank_hits/total}')





