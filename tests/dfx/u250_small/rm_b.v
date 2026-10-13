(* bblock = "true" *)
module rm(input clk, input [7:0] data_in, output reg [7:0] data_out);
  wire [7:0] mixed = data_in ^ {data_in[4:0], data_in[7:5]};
  always @(posedge clk) data_out <= mixed ^ 8'ha5;
endmodule
