# ダミーのPROGMEMデータを作成する

# 作成例　10バイトのPROGMEMデータ
# const byte test_progmem_data_10byte[10] PROGMEM = {
#   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09
# };

def generate_progmem_array(size):
    """指定されたサイズのPROGMEM配列を生成する"""
    print(f"const byte test_progmem_data_{size}byte[{size}] PROGMEM = {{")
    
    for i in range(0, size, 16):
        line_data = []
        for j in range(16):
            if i + j < size:
                value = (i + j) % 256
                line_data.append(f"0x{value:02X}")
        if i + 16 >= size:
            print("  " + ", ".join(line_data))
        else:
            print("  " + ", ".join(line_data) + ",")
    
    print("};")
    print()

# 指定されたサイズの配列を生成する
sizes = [1152, 4608, 18432, 38400, 64000, 115200, 153600]
for size in sizes:
    generate_progmem_array(size)

