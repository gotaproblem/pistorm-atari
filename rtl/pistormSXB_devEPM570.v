/*
 * 13th October 2023
 *
 * EPM240 / 570
 * Has ps_read_status_reg fix
 * Has long hold fix (S7) for 68000
 * 
 * Tested on:
 * 	my STe with Pi3A+/374
 *  Mikerochip microATX ST with Pi4/374
 *
 * Memspeed 3.7/3.7 MB/s
 *
 */

/* Build Definitions */
//`define L374
//`define 
//`define NEWARB

 
module pistormsxb_devEPM570(
    output reg     	PI_TXN_IN_PROGRESS, 	// GPIO0
    output reg     	PI_IPL_ZERO,        	// GPIO1
    input   [1:0]   PI_A,       			// GPIO[3..2]
    input           PI_CLK,     			// GPIO4
    output      	PI_RESET,   			// GPIO5
    input           PI_RD,      			// GPIO6
    input           PI_WR,      			// GPIO7
    inout   [15:0]  PI_D,       			// GPIO[23..8]

    output reg      LTCH_A_0,
    output reg      LTCH_A_8,
    output reg    	LTCH_A_16,
    output reg     	LTCH_A_24,
    output reg      LTCH_A_OE_n,
    output reg      LTCH_D_RD_U,
    output reg      LTCH_D_RD_L,
    output      	LTCH_D_RD_OE_n,
    output      	LTCH_D_WR_U,
    output      	LTCH_D_WR_L,
    output reg      LTCH_D_WR_OE_n,

    input           M68K_CLK,
    output   [2:0]  M68K_FC,

    output       	M68K_AS_n,
    output       	M68K_UDS_n,
    output       	M68K_LDS_n,
    output       	M68K_RW,

    input           M68K_DTACK_n,
    input           M68K_BERR_n,

    input           M68K_VPA_n,
    output reg      M68K_E,
    output       	M68K_VMA_n,

    input   [2:0]   M68K_IPL_n,

    inout           M68K_RESET_n,
    inout           M68K_HALT_n,

    input           M68K_BR_n,
    output       	M68K_BG_n,
    input           M68K_BGACK_n
    //input           M68K_C1,
    //input           M68K_C3,
    //input           CLK_SEL
  );

	wire c200m 								= PI_CLK;
	reg [2:0] c8m_sync;
	//  wire c8m = M68K_CLK;
	wire c8m 								= c8m_sync[2];
	//wire c1c3_clk 							= !(M68K_C1 ^ M68K_C3);

	localparam REG_DATA 					= 2'd0;
	localparam REG_ADDR_LO 					= 2'd1;
	localparam REG_ADDR_HI 					= 2'd2;
	localparam REG_STATUS 					= 2'd3;
	
	localparam LO							= 1'b0;
	localparam HI							= 1'b1;
	
	localparam S0							= 3'd0;
	localparam S1							= 3'd1;
	localparam S2							= 3'd2;
	localparam S3							= 3'd3;
	localparam S4							= 3'd4;
	localparam S5							= 3'd5;
	localparam S6							= 3'd6;
	localparam S7							= 3'd7;
	
	localparam E0							= 4'd0;
	localparam E2							= 4'd2;
	localparam E5							= 4'd5;
	localparam E6							= 4'd6;
	localparam E8							= 4'd8;
	localparam E9							= 4'd9;
	localparam E10							= 4'd10;

	initial begin
		FC_INT 								<= 3'b111;
		RW_INT 								<= HI;
		M68K_E 								<= LO;
		VMA_INT 							<= HI;
		AS_INT 								<= HI;	 
	end

	/* ******************************************** */
	/* 
	 */
	reg [1:0] rd_sync;
	//reg [1:0] wr_sync;  

	always @(posedge c200m) begin
		rd_sync 							<= {rd_sync[0], PI_RD};
	//	wr_sync 							<= {wr_sync[0], PI_WR};
	end

	wire rd_rising = !rd_sync[1] && rd_sync[0];
	//wire wr_rising = !wr_sync[1] && wr_sync[0];
	/* ******************************************** */

	reg [15:0] data_out;

	//wire trigger;
	//assign trigger = (PI_A == REG_STATUS && rd_rising);
	//assign PI_D = trigger ? {ipl, 11'b0, !( M68K_RESET_n || reset_out ), 1'b0} : 16'bz;
	assign PI_D = PI_A == REG_STATUS && PI_RD ? data_out : 16'bz;
	
	always @(posedge c200m) begin
	
		if (rd_rising && PI_A == REG_STATUS) begin
		
			data_out <= {ipl, 11'b0, !( M68K_RESET_n || reset_out ), 1'b0};
		end
	end

	reg [15:0] status;
	wire reset_out 							= !status[1]; /* ps_protocol.c -> ps_write_status_reg (STATUS_BIT_INIT) */

	assign M68K_RESET_n 					= reset_out ? LO : 1'bz;
	assign M68K_HALT_n 						= reset_out ? LO : 1'bz;

	reg op_rw 								= HI;
	reg op_uds_n 							= HI;
	reg op_lds_n 							= HI;

	reg [2:0] FC_INT;
	reg AS_INT;
	reg UDS_INT;
	reg LDS_INT;
	reg RW_INT;
	reg VMA_INT;
	
	assign LTCH_D_WR_U 						= PI_A == REG_DATA && CMDWR;//PI_WR;
	assign LTCH_D_WR_L 						= PI_A == REG_DATA && CMDWR;//PI_WR;
	//assign LTCH_A_0 						= (PI_A == REG_ADDR_LO && PI_WR);
	//assign LTCH_A_8 						= (PI_A == REG_ADDR_LO && PI_WR);
	//assign LTCH_A_16 						= PI_A == REG_ADDR_HI && PI_WR;
	//assign LTCH_A_24 						= PI_A == REG_ADDR_HI && PI_WR;
	assign LTCH_D_RD_OE_n					= !(PI_A == REG_DATA && PI_RD);
	
	
	reg a0;

	always @(posedge c200m) begin
		c8m_sync 							<= {c8m_sync[1:0], M68K_CLK};
	end

	wire c8m_rising 						= !c8m_sync[1] && c8m_sync[0];
	wire c8m_falling 						= c8m_sync[1] && !c8m_sync[0];	
	
	reg [2:0] ipl;
	reg [2:0] reset_d 						= 3'b000;
  
	
	always @(posedge c200m) begin
	
		if ( c8m_falling ) begin
		
			ipl 							<= ~M68K_IPL_n;
		end
			
		reset_d[2:1] 						<= { reset_d[1:0], M68K_RESET_n };
		PI_IPL_ZERO 						<= ( ipl == 3'd0 && reset_d );
	end	
	

	/* M68K_E clock */
	reg [3:0] e_counter 					= E0;
	
	always @(negedge c8m) begin
		
		if ( e_counter == E9 ) begin
		
			e_counter 						<= E0;
			M68K_E 							<= LO;
		end
		
		else if ( e_counter == E5 )
			M68K_E 							<= HI;
			
		e_counter 							<= e_counter + 4'd1;
	end 
	

	/* Bus Arbitration */
	reg [3:0] BG_DELAY 						= 4'b1111;
	reg [3:0] BR_DELAY 						= 4'b1111;
	reg [3:0] BR_DELAYr						= 4'b1111;
	reg [5:0] BGK_DELAY 					= 6'b000000;
	reg [4:0] BGK_DELAYr 					= 5'b00000;
	reg [3:0] BGK_DELAYf 					= 4'b0000;
	reg [3:0] AS_DELAY 						= 4'b1111;
	reg BG_INT								= HI;
	assign M68K_BG_n 						= BG_INT;
	
	reg [3:0] BRstart						= 4'd15;
	
	always @( c8m ) begin // half-cycles
		
		BG_DELAY 							<= { BG_DELAY[3:0],  M68K_BG_n };
		BR_DELAY 							<= { BR_DELAY[3:0],  M68K_BR_n };
		AS_DELAY							<= { AS_DELAY[3:0],  M68K_AS_n };
		BGK_DELAY 							<= { BGK_DELAY[5:0], M68K_BGACK_n };
		
		if (c8m_rising) begin
		
			BGK_DELAYr 						<= { BGK_DELAYr[4:0], M68K_BGACK_n };
			BR_DELAYr 						<= { BR_DELAYr[3:0], M68K_BR_n };
		end
		
		if (c8m_falling) begin
		
			BGK_DELAYf 						<= { BGK_DELAYf[3:0], M68K_BGACK_n };
		end
		
		if ( BR_DELAYr[1:0] == 2'b10 && BRstart == 4'd15 )
			BRstart 						<= state;
		
		/* processor active */
		/* need AS otherwise Blitter and FDD don't work */
		/* if emulating complete bus-cycle then BRstart will be S2 */
		//if ( M68K_BG_n && !BR_DELAYr[2] && !M68K_AS_n && BRstart == S4 && state >= S4 ) begin
		//if ( M68K_BG_n && !BR_DELAY[1] && !M68K_AS_n && BRstart > S0 && state >= S4 ) begin
		if ( M68K_BG_n && !BR_DELAY[1] && !M68K_AS_n && state >= S4 ) begin
		
			if (c8m_falling) begin
			
				BG_INT 						<= LO;
				BRstart						<= 4'd15;
			end
		end
		
		/* bus inactive */
		/*
		else if ( M68K_BG_n && !BR_DELAY[2] && M68K_AS_n == 1'bz && state == S0 ) begin
		
			if (c8m_falling) begin
			
				BG_INT 						<= LO;
				BRstart						<= 4'd15;
			end	
		end
		*/
		/* special case */
		/*
		else if ( M68K_BG_n && BR_DELAYr[3:0] == 4'b0000 && state >= S4 && M68K_BGACK_n  ) begin
		
			if (c8m_falling) begin
			
				BG_INT 						<= LO;
				BRstart						<= 4'd15;
			end
		end
		*/
		else begin
			
			//if ( !M68K_BG_n && !BGK_DELAYf[1] && state == S0 ) begin
			if ( !M68K_BG_n && !BGK_DELAY[1] && state == S0 ) begin
			
				if (c8m_rising)
					BG_INT 					<= HI;
			end
		end
	end	
	
	
	/* Transaction flag */
	wire TXNstart;
	reg TXNreset 							= LO; 
	assign TXNstart 						= (PI_A == REG_ADDR_HI && CMDWR);
	
	always @(TXNstart or TXNreset or PI_TXN_IN_PROGRESS) begin
	
		if (TXNstart)
			PI_TXN_IN_PROGRESS 				<= HI;
		
		else
			if (TXNreset)
				PI_TXN_IN_PROGRESS 			<= LO;
	
	end
	
	
	/* WRITE commands */
	reg [2:0] op_fc 						= 3'b111;
	
	assign CMDWR = PI_WR;
	
	always @(CMDWR) begin
	
		if ( CMDWR ) begin
		
			case (PI_A)
				
				REG_DATA: begin
					//LTCH_D_WR_U				<= HI;
					//LTCH_D_WR_L				<= HI;
				end
				
				REG_ADDR_LO: begin
					a0 						<= PI_D[0];
					LTCH_A_0				<= HI;
					LTCH_A_8				<= HI;
				end
				
				REG_ADDR_HI: begin
					op_rw 					<= PI_D[9];
					op_uds_n 				<= PI_D[8] ? a0 : LO;
					op_lds_n 				<= PI_D[8] ? !a0 : LO;
					op_fc 					<= PI_D[15:13];
					LTCH_A_16				<= HI;
					LTCH_A_24				<= HI;
				end
				
				REG_STATUS: begin
					status 					<= PI_D;
				end
			endcase
		end
		
		else begin
		
			if ( !CMDWR ) begin
			
				LTCH_A_0					<= LO;
				LTCH_A_8					<= LO;
				LTCH_A_16					<= LO;
				LTCH_A_24					<= LO;
				//LTCH_D_WR_U					<= LO;
				//LTCH_D_WR_L					<= LO;
			end
		end
	end 
	
	
	/* State Machine */
	reg read								= 1'b0;
	reg [2:0] state 						= S0;
	reg PI_BERR;
	assign PI_RESET 						= PI_BERR;
	
	always @(posedge c200m) begin
	
		if ( TXNreset )
			TXNreset 						<= LO;
						
		case (state)
		
			/* this first state is needed to sync the bus */
			S0: begin
					
				/* don't run state machine if BGACK asserted */
				if ( BGK_DELAY[0] ) begin
				
					RW_INT 					<= HI;

					/* 374 reset latch */
					LTCH_D_RD_U 			<= LO;
					LTCH_D_RD_L 			<= LO;

					if (c8m_falling) begin
					
						state 				<= S1;
					end
				end
			end

			/* EPM540 delay cycles needed it seems */ 
			
			S1: begin
			
				state 						<= S2;
			end
			
			
			S2: begin
			
				if (PI_TXN_IN_PROGRESS) begin
				
					PI_BERR 				<= HI;
						
					//LTCH_D_WR_OE_n 			<= op_rw;
					LTCH_A_OE_n 			<= LO;
					
					FC_INT 					<= op_fc;
					RW_INT 					<= op_rw;
					
					AS_INT 					<= LO;
					
					if ( op_rw ) begin
				
						UDS_INT 			<= op_uds_n;
						LDS_INT 			<= op_lds_n;
					end
				
					if (c8m_falling) begin
				
						state 				<= S3;
					end
				end
			end
			
			
			S3: begin
			
				if (!op_rw)
					LTCH_D_WR_OE_n 			<= op_rw;
				
				if (c8m_rising) begin
				
					state 					<= S4;
				end
			end
			
			
			S4: begin							
					
				read						<= op_rw;
				
				if ( !op_rw ) begin
			
					UDS_INT 				<= op_uds_n;
					LDS_INT 				<= op_lds_n;
				end
				
				if (c8m_falling) begin
					
					if (!M68K_DTACK_n) begin
						state 				<= S5;
					end
					
					else if (!M68K_BERR_n) begin
						state 				<= S5;
					end
					
					/* IACK bus-cycle TODO */						
					else if (!M68K_VMA_n && e_counter == E8) begin
				
						state 				<= S5;
					end
					
					else begin
			
						if (!M68K_VPA_n && e_counter == E2) begin
						
							VMA_INT			<= LO;
						end
					end
				end
			end


			S5: begin
				
				if (c8m_rising) begin

					/* 374 latch on HI */
					LTCH_D_RD_U 			<= HI;
					LTCH_D_RD_L 			<= HI;
						
					state 					<= S6;
				end
			end   
			
			
			S6: begin
				
				if ( !read ) begin
				
					PI_BERR 			<= M68K_BERR_n;
					TXNreset 			<= HI;
				end
				
				state 						<= S7;
			end
			

			S7: begin				
							
				if ( read ) begin
				
					PI_BERR 				<= M68K_BERR_n;
					TXNreset 				<= HI;
				end
				
				AS_INT 						<= HI;
				UDS_INT 					<= HI;
				LDS_INT 					<= HI;
				VMA_INT 					<= HI;
					
				if (c8m_falling) begin
					
					/* Oct 2023 - long hold as per Claude info for 68000 */
					LTCH_D_WR_OE_n 			<= HI; // data-bus hi-z
					LTCH_A_OE_n 			<= HI; // address-bus hi-z
					
					state 					<= S0;
				end
			end
			
		endcase
	
		
		if ( !M68K_RESET_n && !M68K_HALT_n && !reset_out )  begin
		
			state 							<= S0;
			TXNreset 						<= HI;
		end
		
	end


	assign M68K_FC 							= M68K_BGACK_n ? FC_INT		: 3'bzzz;
	assign M68K_AS_n 						= M68K_BGACK_n ? AS_INT 	: 1'bz;
	assign M68K_UDS_n 						= M68K_BGACK_n ? UDS_INT 	: 1'bz;
	assign M68K_LDS_n 						= M68K_BGACK_n ? LDS_INT 	: 1'bz;
	assign M68K_RW 							= M68K_BGACK_n ? RW_INT 	: 1'bz;
	assign M68K_VMA_n						= M68K_BGACK_n ? VMA_INT 	: 1'bz;
	
endmodule