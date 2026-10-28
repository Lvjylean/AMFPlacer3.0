// Integration fixture: separate RP macros must compete only for their own sites.
module rm(input wire clk, input wire [7:0] data_in, output wire [7:0] data_out);
  wire [7:0] d=data_in; wire [7:0] q; assign data_out=q;
  wire c0,c1,c2,s3,s4;
  (* DONT_TOUCH="yes" *) SRLC32E s0(.CLK(clk),.CE(1'b1),.D(d[0]),.A(5'd31),.Q(),.Q31(c0));
  (* DONT_TOUCH="yes" *) SRLC32E s1(.CLK(clk),.CE(1'b1),.D(c0),.A(5'd31),.Q(),.Q31(c1));
  (* DONT_TOUCH="yes" *) SRLC32E s2(.CLK(clk),.CE(1'b1),.D(c1),.A(5'd31),.Q(),.Q31(c2));
  (* DONT_TOUCH="yes" *) SRL16E srl3(.CLK(clk),.CE(1'b1),.D(d[1]),.A0(d[2]),.A1(d[3]),.A2(d[4]),.A3(d[5]),.Q(s3));
  (* DONT_TOUCH="yes" *) SRL16E srl4(.CLK(clk),.CE(1'b1),.D(d[2]),.A0(d[2]),.A1(d[3]),.A2(d[4]),.A3(d[5]),.Q(s4));
  (* keep="yes" *) reg [31:0] counter=0;
  (* use_dsp="yes" *) reg [33:0] product=0;
  (* ram_style="block" *) reg [15:0] mem[0:1023];
  reg [15:0] rd=0;
  always @(posedge clk) begin
    counter <= counter + {24'b0,d};
    product <= counter[16:0] * {9'b0,d};
    mem[counter[9:0]] <= counter[31:16];
    rd <= mem[counter[19:10]];
  end
  assign q = counter[31:24] ^ product[33:26] ^ rd[7:0] ^ {5'b0,c2,s3,s4};
endmodule
