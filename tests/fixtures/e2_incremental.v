module island(input clk, input d, output [1:0] q);
wire s;
(* DONT_TOUCH="yes" *) FDRE source(.C(clk),.CE(1'b1),.R(1'b0),.D(d),.Q(s));
(* DONT_TOUCH="yes" *) FDRE local_ff(.C(clk),.CE(1'b1),.R(1'b0),.D(s),.Q(q[0]));
(* DONT_TOUCH="yes" *) FDRE remote(.C(clk),.CE(1'b1),.R(1'b0),.D(s),.Q(q[1]));
endmodule
module top(input clk, input [2:0] d, output [4:0] q);
wire c, ssq;
BUFGCE bufg(.I(clk),.CE(1'b1),.O(c));
(* KEEP_HIERARCHY="yes",DONT_TOUCH="yes" *) island a(.clk(c),.d(d[0]),.q(q[1:0]));
(* KEEP_HIERARCHY="yes",DONT_TOUCH="yes" *) island b(.clk(c),.d(d[1]),.q(q[3:2]));
(* DONT_TOUCH="yes" *) FDRE static_source(.C(c),.CE(1'b1),.R(1'b0),.D(d[2]),.Q(ssq));
(* DONT_TOUCH="yes" *) FDRE static_sink(.C(c),.CE(1'b1),.R(1'b0),.D(ssq),.Q(q[4]));
endmodule
