csrrs x5,0x803,x0       803022f3        x5  = 90014000
lw x10,0(x5)            0002a503        x10 = 90000000
vid.v v0                5208a057        v0  = 7,6,5,4,3,2,1,0
vsll.vi v0,v0,2         96013057        v0  = 1c,18,14,10,0c,08,04,00
vadd.vx v31,v0,x10      02054FD7        v31 = 9000001c,90000018,...
vlw12.v v0,0(v31)       000FA07b        v1  = 随机整数
vlw12.v v1,0x10(v31)    010FA0Fb        v2  = 
vlw12.v v2,0x20(v31)    020FA17B        v3  = 
vlw12.v v3,0x30(v31)    030FA1FB        v4  = 
vlw12.v v4,0x40(v31)    040FA27B        v5  = 
vlw12.v v5,0x50(v31)    050FA2FB
vlw12.v v6,0x60(v31)    060FA37B
vlw12.v v7,0x70(v31)    070FA3FB
vlw12.v v8,0x80(v31)    080FA47B
vlw12.v v9,0x90(v31)    090FA4FB
vlw12.v v10,0x100(v31)  100FA57B
vlw12.v v11,0x110(v31)  110FA5FB
vftta.v v8,v0,v4,v8     0E40440B
vftta.v v9,v0,v6,v9     0E60448B
vftta.v v10,v2,v4,v10   0E41450B
vftta.v v11,v2,v6,v11   0E61458B
vftta.v v8,v1,v5,v8     0E50C40B
vftta.v v9,v1,v7,v9     0E70C48B
vftta.v v10,v3,v5,v10   0E51C50B
vftta.v v11,v3,v7,v11   0E71C58B
endprg                  0000400b
addi x2,x1,0            00008113
addi x2,x1,0            00008113
addi x2,x1,0            00008113
addi x2,x1,0            00008113
addi x2,x1,0            00008113
addi x2,x1,0            00008113
addi x2,x1,0            00008113
addi x2,x1,0            00008113


生成tensor.data后，再后面附上上述指令
最后还要加上
90000000


