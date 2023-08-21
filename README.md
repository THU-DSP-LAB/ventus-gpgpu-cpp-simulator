Requirements:

- GCC version >= 11  
- enable C++20 support
- to use the latest release of SystemC is highly recommended

Please scroll down for the English version of README.

---

配置systemc可以参考我的[博文](https://zhuanlan.zhihu.com/p/638360098)（也参考了很多别人的经验，但这篇比较适合本工程）。

主程序目前在 sc-code/sm 中，在sm文件夹下运行【make -j $(nproc) && ./ventus.out --inssrc imem --metafile matadd/matadd.metadata --datafile matadd/matadd.data --numcycle 10000】来编译程序并运行。

出现Segmentation fault的调试方法：  
参考[Linux下Segmentation Fault的定位方法](https://blog.csdn.net/whahu1989/article/details/110881842)、[linux下不产生core文件的原因](https://blog.csdn.net/qq_35621436/article/details/120870746)。  
调试步骤（调试完记得把core文件删除，文件太大上传到github会报错）：

```bash
ulimit -c unlimited
sudo bash -c "echo core > /proc/sys/kernel/core_pattern "
make GPGPU_test_GDB -j $(nproc)
./ventus --inssrc imem --metafile matadd/matadd.metadata --datafile matadd/matadd.data --numcycle 10000
gdb ./ventus ./core
```

---

To configure systemc, you can refer to my [blog post](https://zhuanlan.zhihu.com/p/638360098) (I also referred to the experience of many others, but this one is more suitable for this project).

The main program is currently in sc-code/sm, run [make -j $(nproc) && ./ventus.out --inssrc imem --metafile matadd/matadd.metadata --datafile matadd/matadd.data --numcycle 10000] to compile the program and get the output under the sm folder.
