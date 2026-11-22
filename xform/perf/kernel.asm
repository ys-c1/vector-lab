
build/bench:     file format elf64-x86-64


Disassembly of section .init:

Disassembly of section .plt:

Disassembly of section .plt.got:

Disassembly of section .plt.sec:

Disassembly of section .text:

0000000000002200 <xform_apply_batch_strided>:
    2200:	f3 0f 1e fa          	endbr64
    2204:	49 89 f1             	mov    %rsi,%r9
    2207:	48 89 d6             	mov    %rdx,%rsi
    220a:	49 89 fa             	mov    %rdi,%r10
    220d:	4d 85 c9             	test   %r9,%r9
    2210:	0f 94 c0             	sete   %al
    2213:	48 83 fa 0f          	cmp    $0xf,%rdx
    2217:	0f 96 c2             	setbe  %dl
    221a:	08 d0                	or     %dl,%al
    221c:	0f 85 fe 00 00 00    	jne    2320 <xform_apply_batch_strided+0x120>
    2222:	48 85 ff             	test   %rdi,%rdi
    2225:	0f 84 f5 00 00 00    	je     2320 <xform_apply_batch_strided+0x120>
    222b:	48 85 c9             	test   %rcx,%rcx
    222e:	b8 00 01 00 00       	mov    $0x100,%eax
    2233:	f3 0f 7e 64 24 08    	movq   0x8(%rsp),%xmm4
    2239:	66 0f 16 64 24 10    	movhpd 0x10(%rsp),%xmm4
    223f:	f3 0f 7e 5c 24 18    	movq   0x18(%rsp),%xmm3
    2245:	66 0f 16 5c 24 20    	movhpd 0x20(%rsp),%xmm3
    224b:	48 0f 45 c1          	cmovne %rcx,%rax
    224f:	31 d2                	xor    %edx,%edx
    2251:	f3 0f 7e 54 24 28    	movq   0x28(%rsp),%xmm2
    2257:	66 0f 16 54 24 30    	movhpd 0x30(%rsp),%xmm2
    225d:	49 89 c0             	mov    %rax,%r8
    2260:	31 c0                	xor    %eax,%eax
    2262:	4c 89 c7             	mov    %r8,%rdi
    2265:	48 01 c7             	add    %rax,%rdi
    2268:	0f 92 c2             	setb   %dl
    226b:	49 39 f9             	cmp    %rdi,%r9
    226e:	0f 82 97 00 00 00    	jb     230b <xform_apply_batch_strided+0x10b>
    2274:	90                   	nop
    2275:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
    227c:	00 00 00 00 
    2280:	48 85 d2             	test   %rdx,%rdx
    2283:	0f 85 82 00 00 00    	jne    230b <xform_apply_batch_strided+0x10b>
    2289:	48 89 f9             	mov    %rdi,%rcx
    228c:	31 d2                	xor    %edx,%edx
    228e:	48 29 c1             	sub    %rax,%rcx
    2291:	48 0f af c6          	imul   %rsi,%rax
    2295:	4c 01 d0             	add    %r10,%rax
    2298:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
    229f:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
    22a6:	00 00 00 00 
    22aa:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
    22b1:	00 00 00 00 
    22b5:	66 66 2e 0f 1f 84 00 	data16 cs nopw 0x0(%rax,%rax,1)
    22bc:	00 00 00 00 
    22c0:	f3 0f 6f 00          	movdqu (%rax),%xmm0
    22c4:	48 83 c2 01          	add    $0x1,%rdx
    22c8:	66 0f 6f c8          	movdqa %xmm0,%xmm1
    22cc:	66 0f 6d c8          	punpckhqdq %xmm0,%xmm1
    22d0:	66 0f 6c c0          	punpcklqdq %xmm0,%xmm0
    22d4:	66 0f 59 cb          	mulpd  %xmm3,%xmm1
    22d8:	66 0f 59 c4          	mulpd  %xmm4,%xmm0
    22dc:	66 0f 58 c1          	addpd  %xmm1,%xmm0
    22e0:	66 0f 58 c2          	addpd  %xmm2,%xmm0
    22e4:	0f 11 00             	movups %xmm0,(%rax)
    22e7:	48 01 f0             	add    %rsi,%rax
    22ea:	48 39 ca             	cmp    %rcx,%rdx
    22ed:	75 d1                	jne    22c0 <xform_apply_batch_strided+0xc0>
    22ef:	4c 39 cf             	cmp    %r9,%rdi
    22f2:	73 2d                	jae    2321 <xform_apply_batch_strided+0x121>
    22f4:	48 89 f8             	mov    %rdi,%rax
    22f7:	4c 89 c7             	mov    %r8,%rdi
    22fa:	31 d2                	xor    %edx,%edx
    22fc:	48 01 c7             	add    %rax,%rdi
    22ff:	0f 92 c2             	setb   %dl
    2302:	49 39 f9             	cmp    %rdi,%r9
    2305:	0f 83 75 ff ff ff    	jae    2280 <xform_apply_batch_strided+0x80>
    230b:	4c 39 c8             	cmp    %r9,%rax
    230e:	73 10                	jae    2320 <xform_apply_batch_strided+0x120>
    2310:	4c 89 cf             	mov    %r9,%rdi
    2313:	e9 71 ff ff ff       	jmp    2289 <xform_apply_batch_strided+0x89>
    2318:	0f 1f 84 00 00 00 00 	nopl   0x0(%rax,%rax,1)
    231f:	00 
    2320:	c3                   	ret
    2321:	c3                   	ret

Disassembly of section .fini:
