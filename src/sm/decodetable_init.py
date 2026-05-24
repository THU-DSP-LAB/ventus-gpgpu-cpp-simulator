# 导入必要的库
import re
import os
from tabulate import tabulate
import openpyxl

# 定义解析规则的正则表达式
pattern = r"^(\w+)\s*->\s*List\(([\w\s\.\,]+)\)"

current_dir = os.path.dirname(__file__)
scalafile = os.path.join(current_dir, "DecodeUnit.scala")

# 打开要读取的文件
with open(scalafile, "r") as f:
    # 消除每行开头空行
    lines = [line.strip() for line in f if line.strip()]

# 创建输出文件
cppfile = os.path.join(current_dir, "init_decodetable.cpp")
supportfile = os.path.join(current_dir, "supported_instructions.txt")

opcodes = []
data = []

with open(cppfile, "w") as out_file:
    # 逐行读取输入文件内容
    out_file.write(
        '#include "../parameters.h"\n' +
        '#include <map>\n' +
        '#include <memory>\n' +
        'std::shared_ptr<std::map<OP_TYPE, decodedat>> gen_decodetable(){\n' +
        'return std::make_shared<std::map<OP_TYPE, decodedat>>(std::map<OP_TYPE, decodedat>({\n'
    )
    for line in lines:
        # 使用正则表达式匹配当前行的内容
        match = re.match(pattern, line)
        if match:
            # 获取指令名称
            opcode = match.group(1)
            # print("opcode =", opcode)
            # 将指令名称中的“->”替换为下划线
            opcode = opcode.replace("->", "_")
            opcodes.append(opcode)
            # 获取指令参数列表
            params = match.group(2).split(",")
            # 删除每一项的所有空格
            for i in range(len(params)):
                params[i] = params[i].strip().replace(" ", "")
            # 对每个参数进行处理
            processed_params = []
            reduced_params = []
            for param in params:
                # 如果参数为N或Y，则将其转换为0或1
                if param == "N":
                    processed_params.append("0")
                    reduced_params.append("0")
                elif param == "Y":
                    processed_params.append("1")
                    reduced_params.append("1")
                else:
                    # 如果参数包含“.”，则将“.”转换为下划线
                    if "." in param:
                        param = param.replace(".", "_")
                    reduced_params.append(param)
                    # 在参数前添加“DecodeParams::”
                    param = "DecodeParams::" + param
                    processed_params.append(param)
            data.append({"opcode": opcode, "params": reduced_params})
            # 将处理后的参数列表转换为C++代码形式
            code_params = ", ".join(processed_params)
            # 将结果写入输出文件
            out_file.write("{{{0}_, {{{1}}}}}, \n".format(opcode, code_params))
    out_file.write("}));\n}")

opcodes.sort()
with open(supportfile, "w") as supportinsf:
    supportinsf.write(",\n".join(opcodes))

headers = [
    "EXEC",
    "op",
    "isvec",
    "fp",
    "barrier",
    "bran",
    "simtSTK",
    "simtop",
    "csr",
    "rever",
    "selalu3",
    "selalu2",
    "selalu1",
    "selimm",
    "memwhb",
    "alufn",
    "mul",
    "memcmd",
    "mem_un",
    "fence",
    "sfu",
    "wvd",
    "rdmask",
    "wrtmask",
    "wxd",
    "tc",
    "dismask",
    "custom",
    "atomic",
]
# 构建表格数据
table_data = []


class decodedat:
    def __init__(
        self,
        isvec,
        fp,
        barrier,
        branch,
        simt_stack,
        simt_stack_op,
        csr,
        reverse,
        sel_alu3,
        sel_alu2,
        sel_alu1,
        sel_imm,
        mem_whb,
        alu_fn,
        mul,
        mem_cmd,
        mem_unsigned,
        fence,
        sfu,
        wvd,
        readmask,
        writemask,
        wxd,
        tc,
        disable_mask,
        undefined1,
        undefined2,
    ):
        self.isvec = isvec
        self.fp = fp
        self.barrier = barrier
        self.branch = branch
        self.simt_stack = simt_stack
        self.simt_stack_op = simt_stack_op
        self.csr = csr
        self.reverse = reverse
        self.sel_alu3 = sel_alu3
        self.sel_alu2 = sel_alu2
        self.sel_alu1 = sel_alu1
        self.sel_imm = sel_imm
        self.mem_whb = mem_whb
        self.alu_fn = alu_fn
        self.mul = mul
        self.mem_cmd = mem_cmd
        self.mem_unsigned = mem_unsigned
        self.fence = fence
        self.sfu = sfu
        self.wvd = wvd
        self.readmask = readmask
        self.writemask = writemask
        self.wxd = wxd
        self.tc = tc
        self.disable_mask = disable_mask
        self.undefined1 = undefined1
        self.undefined2 = undefined2


for entry in data:
    # print(entry)
    old_params = entry["params"]
    myclass = decodedat(*old_params)
    if myclass.tc != "0":
        execunit = "TC"
    elif myclass.sfu != "0":
        execunit = "SFU"
    elif myclass.fp != "0":
        execunit = "VFPU"
    elif myclass.csr != "CSR_N":
        execunit = "CSR"
    elif myclass.mul != "0":
        execunit = "MUL"
    elif myclass.mem_cmd != "M_X":
        execunit = "LSU"
    elif myclass.isvec != "0":
        if entry["opcode"] == "JOIN":
            execunit = "SIMTSTK"
        else:
            execunit = "VALU"
    elif myclass.barrier != "0":
        execunit = "WPSCHEDLER"
    else:
        execunit = "SALU"
    entry["params"] = entry["params"] + [execunit]


def sort_key(element):
    params = element["params"]
    last_param = params[-1]  # 获取最后一个元素
    return (last_param, element["opcode"])  # 返回一个元组，用于比较排序


data = sorted(data, key=sort_key)
for entry in data:
    row = [entry["params"][-1]] + [entry["opcode"]] + entry["params"][0:-1]
    table_data.append(row)


# 使用tabulate生成Markdown表格
markdown_table = tabulate(table_data, headers, tablefmt="pipe")
# 保存Markdown表格到文件
with open(os.path.join(current_dir, "decodetable.md"), "w") as f:
    f.write(markdown_table)




# 创建一个新的Excel工作簿
workbook = openpyxl.Workbook()

# 选择默认的工作表
sheet = workbook.active

sheet.append(headers)


for row in table_data:
    sheet.append(row)

# 保存工作簿为Excel文件
workbook.save(os.path.join(current_dir, "decodetable.xlsx"))

# 检测额外的Decode结果
def checkExtDecode():
    '''
    寻找./DecodeUnit.scala文件中以“    c.”开头的行作为候选行
    遍历所有候选行列表candlist：
        如果候选行有s(xx)，其中xx是数字，则放到不需处理的列表safe_list
    遍历列表safe_list：
        定义temp = 列表的某行
        删掉temp//后的注释，
        如果temp在:=后面除了s(xx)外还有其他非空字符，
        则把这个条目从列表safe_list中删掉
    return candlist-safe_list

    将candlist-safe_list内容逐行保存到文件warning-ctrlSig.log中
    '''
    candlist = []
    safe_list = []

    # 读入文件，找以"    c."开头的行
    with open('DecodeUnit.scala', 'r',encoding="UTF-8") as f:
        for line in f:
            if line.startswith('    c.'):
                candlist.append(line.rstrip('\n'))

    # 第一轮筛选：找包含 s(xx) 的行
    for line in candlist:
        if re.search(r's\(\d+\)', line):
            safe_list.append(line)

    # 第二轮处理 safe_list
    filtered_safe_list = []
    for temp in safe_list:
        # 去掉注释
        temp_nocomment = temp.split('//')[0].strip()
        # 分割":="
        if ':=' in temp_nocomment:
            lhs, rhs = temp_nocomment.split(':=', 1)
            rhs = rhs.strip()
            # 移除所有 s(xx)
            rhs_clean = re.sub(r's\(\d+\)', '', rhs)
            if rhs_clean.strip() == '':
                filtered_safe_list.append(temp)
            # 如果除了s(xx)还有别的内容，不保留
        else:
            # 如果没有 :=，默认保留
            filtered_safe_list.append(temp)

    warning_list = [line for line in candlist if line not in filtered_safe_list]
    
    print(f"[Info] 总计需要手工处理的额外Decode信号数量：{len(warning_list)}")

    warning_list_1 = []
    with open('decodeExt.txt', 'r',encoding="UTF-8") as f:
        lines_ok = f.readlines()
        lines_ok = [l.replace('\n', "") for l in lines_ok]
        # warning_list中的某行若已在f中存在，说明已确认手工处理完毕，删除之
        for line in warning_list:
            if line not in lines_ok:
                warning_list_1.append(line)

    print(f"[Info] 滤除decodeExt.txt中表示已手工处理完毕的额外Decode信号，仍剩余：{len(warning_list_1)}")

    # 如果warning_list_1为空，删除decodeExt_warn.txt文件
    # 否则将warning_list_1中的内容写入decodeExt_warn.txt
    if len(warning_list_1) == 0:
        if os.path.exists('decodeExt_warn.txt'):
            os.remove('decodeExt_warn.txt')
            print("[INFO] All OK")
    else:
        with open('decodeExt_warn.txt', 'w') as f:
            for line in warning_list_1:
                f.write(line + '\n')
            print("[WARN] 结果已保存到 decodeExt_warn.txt")

checkExtDecode()
