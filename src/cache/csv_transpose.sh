#!/bin/bash

# 读取CSV文件名
#echo "请输入CSV文件名（包括扩展名）："
csv_file="$1"

# 在原地转置CSV文件
awk -F "," '
{
  for (i=1; i<=NF; i++) {
    if (NR==1) {
      # 第一行，创建数组
      header[i]=$i
      data[i]=$i
    } else {
      # 非第一行，添加数据到数组
      data[i]=data[i]","$i
    }
  }
}
END {
  # 输出转置后的CSV文件
  for (i=1; i<=NF; i++) {
    print data[i]
  }
}' "$csv_file" > tmp.csv && mv tmp.csv "$csv_file"

echo "转置完成！"
