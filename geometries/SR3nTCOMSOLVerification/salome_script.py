from salome.shaper import model
"""
Direct transcription of the COMSOL matlab code 
"""

## ------------   Constants --------------------------
from dataclasses import dataclass
import math

@dataclass
class GeometryParams:
	# Diameter of the field shaping rings
	WireDiameter: float = 0.002  # 2 [mm]
	# Radial position of the center of the field shaping rings
	WireRadialPosition: float = 0.668  # 668 [mm]
	# Vertical pitch of the field shaping rings in warm condition
	WireVerticalPitch: float = 0.022  # 22 [mm]
	# Vertical position of the top most field shaping ring in warm condition
	WireTopVerticalPosition: float = -0.023  # -23 [mm]	# WARNING: multiple values: ['-12 [mm]', '-23 [mm]', '12 [mm]', '17 [mm]']
	# Position of the top face of the gate electrode (z=0 by definition)
	GateVerticalPosition: float = 0  # 0 [mm]
	# Position of the bottom face of the anode electrode
	AnodeVerticalPosition: float = 0.008  # 8 [mm]	# WARNING: multiple values: ['26 [mm]', '8 [mm]']
	# Position of the top face of the top screen electrode
	TopScreenVerticalPosition: float = 0.055  # 55 [mm]	# WARNING: multiple values: ['51 [mm]', '55 [mm]', '56 [mm]']
	# Height of the top screen electrode frame
	TopScreenHeight: float = 0.015  # 15 [mm]
	# Width of the top screen electrode frame
	TopScreenWidth: float = 0.031  # 31 [mm]
	# Deburring radius of the edges of the top screen electrode radius
	TopScreenDeburringRadius: float = 0.0016  # 1.6 [mm]
	# Height of the anode electrode frame
	AnodeHeight: float = 0.024  # 24 [mm]	# WARNING: multiple values: ['18 [mm]', '24 [mm]']
	# Width of the anode electrode frame
	AnodeWidth: float = 0.031  # 31 [mm]
	# Deburring radius of the anode electrode frame
	AnodeDeburringRadius: float = 0.0016  # 1.6 [mm]
	# Height of the gate electrode frame
	GateHeight: float = 0.02  # 20 [mm]
	# Height of the cut-out of the gate electrode frame
	GateCutOutHeight: float = 0.011  # 11 [mm]
	# Width of the gate electrode frame
	GateWidth: float = 0.031  # 31 [mm]
	# Width of the cut-out of the gate electrode frame
	GateCutOutWidth: float = 0.01  # 10 [mm]
	# Deburring radius of the gate electrode frame
	GateDeburringRadius: float = 0.0016  # 1.6 [mm]
	# Radial position of the inner face of the anode electrode frame
	AnodeRadialPosition: float = 0.667  # 667 [mm]
	# Radial position of the inner face of the gate electrode frame
	GateRadialPosition: float = 0.667  # 667 [mm]
	# Radial position of the inner face of the top screen electrode frame
	TopScreenRadialPosition: float = 0.667  # 667 [mm]
	# Radial position of the inner face of the electrodes of the top stack
	TopStackRadialPosition: float = 0.667  # 667 [mm]
	# Spacing between wires for the top stack electriodes
	TopStackWireSpacing: float = 0.005  # 5 [mm]
	# Diameter of the wires of the top screen electrode
	TopScreenWireDiameter: float = 0.000216  # 216 [um]
	# Diameter of the wires of the gate electrode
	GateWireDiameter: float = 0.000216  # 216 [um]
	# Diameter of the wires of the electrodes of the top stack
	TopStackWireDiameter: float = 0.000216  # 216 [um]
	# Vertical position of the top face of the insulating frame of the top stack electrodes
	TopStackInsulationVerticalPosition: float = 0.055  # 55 [mm]
	# Radial position of the inner face of the insulating frame of the top stack electrodes
	TopStackInsulationRadialPosition: float = 0.6643  # 664.3 [mm]
	# Width of the insulating frame of the top stack electrodes
	TopStackInsulationWidth: float = 0.0362  # 36.2 [mm]
	# Height of the insulating frame of the top stack electrodes
	TopStackInsulationHeight: float = 0.075  # 75 [mm]
	# Distance between the end of the insulating frame and the electrode wires for the top stack electrodes (only top screen and anode)
	TopStackInsulationWireGap: float = 0.0007  # 0.7 [mm]
	# Height of the reflector of the top screen electrode, meaning the inner part of the insulating frame
	TopScreenReflectorHeight: float = 0.0332  # 33.2 [mm]
	# Height of the insulating frame of the top screen electrode
	TopScreenInsulationHeight: float = 0.0183  # 18.3 [mm]
	# Distance between the end of the insulating frame and the electrode wires for the top stack electrodes (only top screen and anode, insulation on top, electrode on bottom)
	TopStackInsulationWireGapTop: float = 0.0007  # 0.7 [mm]
	# Distance between the end of the insulating frame and the electrode wires for the top stack electrodes (only top screen and anode, insulation on bottom, electrode on top)
	TopStackInsulationWireGapBottom: float = 0.0005  # 0.5 [mm]
	# Dimension A of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionA: float = 0.0308  # 30.8 [mm]	# WARNING: multiple values: ['26.8 [mm]', '30.8 [mm]']
	# Dimension B of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionB: float = 0.0005  # 0.5 [mm]
	# Dimension C of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionC: float = 0.012  # 12 [mm]	# WARNING: multiple values: ['0.5 [mm]', '12 [mm]']
	# Dimension D of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionD: float = 0.0202  # 20.2 [mm]	# WARNING: multiple values: ['20.2 [mm]', '26.8 [mm]']
	# Dimension E of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionE: float = 0.004  # 4 [mm]	# WARNING: multiple values: ['26.8 [mm]', '4 [mm]']
	# Dimension F of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionF: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['2 [mm]', '26.8 [mm]']
	# Dimension G of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionG: float = 0.0315  # 31.5 [mm]	# WARNING: multiple values: ['26.8 [mm]', '27.5 [mm]', '31.5 [mm]']
	# Dimension H of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionH: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['2 [mm]', '26.8 [mm]']
	# Dimension I of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionI: float = 0.0222  # 22.2 [mm]	# WARNING: multiple values: ['22.2 [mm]', '26.8 [mm]']
	# Dimension J of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionJ: float = 0.0005  # 0.5 [mm]	# WARNING: multiple values: ['0.5 [mm]', '26.8 [mm]']
	# Dimension K of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionK: float = 0.0027  # 2.7 [mm]	# WARNING: multiple values: ['2.7 [mm]', '26.8 [mm]']
	# Vertical position of the bottom left corner of the insulating frame of the anode electrode frame
	AnodeInsulationVerticalPosition: float = 0.0087  # 8.7 [mm]
	# Dimension A of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionA: float = 0.026  # 26 [mm]	# WARNING: multiple values: ['26 [mm]', '26.8 [mm]']
	# Dimension B of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionB: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['2 [mm]', '26 [mm]']
	# Dimension C of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionC: float = 0.004  # 4 [mm]	# WARNING: multiple values: ['26 [mm]', '4 [mm]']
	# Dimension D of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionD: float = 0.0202  # 20.2 [mm]	# WARNING: multiple values: ['20.2 [mm]', '26 [mm]']
	# Dimension E of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionE: float = 0.012  # 12 [mm]	# WARNING: multiple values: ['12 [mm]', '26 [mm]']
	# Dimension F of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionF: float = 0.0005  # 0.5 [mm]	# WARNING: multiple values: ['0.5 [mm]', '26 [mm]']
	# Dimension G of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionG: float = 0.007  # 7 [mm]	# WARNING: multiple values: ['26 [mm]', '7 [mm]']
	# Dimension H of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionH: float = 0.0005  # 0.5 [mm]	# WARNING: multiple values: ['0.5 [mm]', '26 [mm]']
	# Dimension I of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionI: float = 0.0222  # 22.2 [mm]	# WARNING: multiple values: ['22.2 [mm]', '26 [mm]']
	# Dimension J of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionJ: float = 0.02  # 20 [mm]	# WARNING: multiple values: ['20 [mm]', '26 [mm]']
	# Dimension K of the gate insulating frame (see drawing GateInsulatingFrame)
	GateInsulationDimensionK: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['2 [mm]', '26 [mm]']
	# Vertical position of the bottom left corner of the insulating frame of the gate electrode frame
	GateInsulationVerticalPosition: float = 0.0005  # 0.5 [mm]	# WARNING: multiple values: ['- 9 [mm]', '0.5 [mm]', '8.7 [mm]']
	# Dimension L of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionL: float = 0.0255  # 25.5 [mm]	# WARNING: multiple values: ['19.5 [mm]', '25.2 [mm]', '25.5 [mm]', '27.5 [mm]']
	# Dimension A of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionA: float = 0.0165  # 16.5 [mm]	# WARNING: multiple values: ['16.5 [mm]', '4 [mm]']
	# Dimension B of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionB: float = 0.0005  # 0.5 [mm]	# WARNING: multiple values: ['0.5 [mm]', '16.5 [mm]']
	# Dimension C of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionC: float = 0.0035  # 3.5 [mm]	# WARNING: multiple values: ['16.5 [mm]', '3.5 [mm]']
	# Dimension D of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionD: float = 0.0335  # 33.5 [mm]	# WARNING: multiple values: ['16.5 [mm]', '33.5 [mm]']
	# Dimension E of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionE: float = 0.0149  # 14.9 [mm]	# WARNING: multiple values: ['14.9 [mm]', '16.5 [mm]']
	# Dimension F of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionF: float = 0.0027  # 2.7 [mm]	# WARNING: multiple values: ['16.5 [mm]', '2.7 [mm]']
	# Dimension G of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionG: float = 0.0332  # 33.2 [mm]	# WARNING: multiple values: ['16.5 [mm]', '33.2 [mm]']
	# Dimension H of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionH: float = 0.0093  # 9.3 [mm]	# WARNING: multiple values: ['16.5 [mm]', '9.3 [mm]']
	# Dimension I of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionI: float = 0.0222  # 22.2 [mm]	# WARNING: multiple values: ['16.5 [mm]', '22.2 [mm]']
	# Dimension J of the top screen insulating frame (see drawing TopScreenInsulatingFrame)
	TopScreenInsulationDimensionJ: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['16.5 [mm]', '2 [mm]']
	# Vertical position of the bottom left corner of the insulating frame of the top screen electrode frame
	TopScreenInsulationVerticalPosition: float = 0.0407  # 40.7 [mm]	# WARNING: multiple values: ['36.7 [mm]', '40.7 [mm]']
	# Vertical position of the top face of the reflecting panel
	PanelVerticalPosition: float = -0.0008  # -0.8 [mm]	# WARNING: multiple values: ['-0.8 [mm]', '0.8 [mm]']
	# Width of the reflecting panel
	PanelWidth: float = 0.003  # 3 [mm]
	# Radial position of the face of the reflecting panel facing the inner volume of the TPC
	PanelRadialPosition: float = 0.664  # 664 [mm]
	# Height of the reflecting panel
	PanelHeight: float = 1.5008  # 1500.8 [mm]
	# Shrinkage of the PTFE at the liquid xenon temperature
	ShrinkageFactor: float = 0.0  # original: '1-0.014'	# WARNING: multiple values: ['1', '1-0.014']
	# Inner diameter of the inner cryostat
	CryostatDiameter: float = 1.46  # 1460 [mm]	# WARNING: multiple values: ['1460 [mm]', '730 [mm]']
	# Vertical position of the top copper ring
	CopperRingVerticalPosition: float = -0.025  # -25 [mm]
	# Height of the top copper ring
	CopperRingHeight: float = 0.01  # 10 [mm]
	# Width of the top copper ring
	CopperRingWidth: float = 0.021  # 21 [mm]	# WARNING: multiple values: ['21 [mm]', '21 [mm] - CopperRingModificationTemporary']
	# Radial position of the inner face of the top copper ring
	CopperRingRadialPosition: float = 0.679  # 679 [mm]	# WARNING: multiple values: ['679 [mm]', '679 [mm] +CopperRingModificationTemporary', '679 [mm] -CopperRingModificationTemporary']
	# Deburring radius of the top copper ring
	CopperRingDeburringRadius: float = 0.0015  # 1.5 [mm]
	# Height of the insulator of the top copper ring (placed between the gate and the copper ring)
	CopperRingInsulatorHeight: float = 0.005  # 5 [mm]
	# Width of the insulator of the top copper ring
	CopperRingInsulatorWidth: float = 0.028  # 28 [mm]
	# Radial position of the inner face of the insulator of the top copper ring
	CopperRingInsulatorRadialPosition: float = 0.679  # 679 [mm]
	# Vertical position of the top face of the insulator of the top copper ring
	CopperRingInsulatorVerticalPosition: float = -0.02  # -20 [mm]
	# Height of the insulator of the top copper ring (placed between the gate and the copper ring)
	CopperRingInsulationHeight: float = 0.005  # 5 [mm]
	# Radial position of the inner face of the insulator of the top copper ring
	CopperRingInsulationRadialPosition: float = 0.679  # 679 [mm]
	# Vertical position of the top face of the insulator of the top copper ring
	CopperRingInsulationVerticalPosition: float = -0.02  # -20 [mm]
	# Width of the insulator of the top copper ring
	CopperRingInsulationWidth: float = 0.028  # 28 [mm]
	# Spacing between wires for the bottom stack electriodes
	BottomStackWireSpacing: float = 0.0075  # 7.5 [mm]	# WARNING: multiple values: ['5 [mm]', '7.5 [mm]']
	# Height of the cathode electrode frame
	CathodeHeight: float = 0.02  # 20 [mm]
	# Width of the cathode electrode frame
	CathodeWidth: float = 0.024  # 24 [mm]
	# Deburring radius of the cathode electrode frame
	CathodeDeburringRadius: float = 0.0005  # 0.5 [mm]
	# Radius of the rounding of the cathode electrode frame
	CathodeRoundingRadius: float = 0.01  # 10 [mm]
	# Vertical position of the upper face of the cathode electrode frame
	CathodeVerticalPosition: float = -1.5028  # -1502.8 [mm]
	# Diameter of the wires of the cathode electrode
	CathodeWireDiameter: float = 0.0003  # 300 [um]
	# Radial position of the cathode electrode frame
	CathodeRadialPosition: float = 0.6735  # 673.5 [mm]
	# Radial position of the inner surface of the bottom screen electrode frame
	BottomScreenRadialPosition: float = 0.6725  # 672.5 [mm]
	# Width of the bottom screen electrode frame
	BottomScreenWidth: float = 0.025  # 25 [mm]
	# Hieght of the bottom screen electrode frame
	BottomScreenHeight: float = 0.015  # 15 [mm]
	# Deburring radius of the bottom screen electrode frame
	BottomScreenDeburringRadius: float = 0.0005  # 0.5 [mm]
	# Rounding radious of the bottom screen electrode frame
	BottomScreenRoundingRadius: float = 0.0075  # 7.5 [mm]
	# Vertical position of the lower face of the bottom screen electrode frame
	BottomScreenVerticalPosition: float = -1.558  # -1558 [mm]	# WARNING: multiple values: ['-1558 [mm]', '1558 [mm]']
	# Diameter of the wires of the bottom screen electrode
	BottomScreenWireDiameter: float = 0.000216  # 216 [um]
	# Dimension A of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionA: float = 0.01  # 10 [mm]
	# Dimension B of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionB: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['10 [mm]', '2 [mm]']
	# Dimension C of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionC: float = 0.012  # 12 [mm]	# WARNING: multiple values: ['10 [mm]', '12 [mm]']
	# Dimension D of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionD: float = 0.007  # 7 [mm]	# WARNING: multiple values: ['10 [mm]', '7 [mm]']
	# Dimension E of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionE: float = 0.008  # 8 [mm]	# WARNING: multiple values: ['10 [mm]', '8 [mm]']
	# Dimension F of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionF: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['10 [mm]', '2 [mm]']
	# Dimension G of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionG: float = 0.012  # 12 [mm]	# WARNING: multiple values: ['10 [mm]', '12 [mm]']
	# Dimension H of the cathode insulating frame (see drawing CathodeInsulatingFrame)
	CathodeInsulationDimensionH: float = 0.005  # 5 [mm]	# WARNING: multiple values: ['12 [mm]', '5 [mm]']
	# Radial position of the insulating frame of the cathode electrode frame
	CathodeInsulationRadialPosition: float = 0.6855  # 685.5 [mm]
	# Vertical position of the insulating frame of the cathode electrode frame
	CathodeInsulationVerticalPosition: float = -1.5028  # -1502.8 [mm]	# WARNING: multiple values: ['-1502.8 [mm]', '1502.8 [mm]']
	# Height of the bottom stack reflector
	BottomStackInsulatorHeight: float = 0.05378  # 53.78 [mm]
	# Width of the bottom stack reflector+
	BottomStackInsulationWidth: float = 0.0042  # 4.2 [mm]
	# Height of the bottom stack reflector
	BottomStackInsulationHeight: float = 0.05378  # 53.78 [mm]
	# Vertical position of the top face of the bottom stack reflector
	BottomStackInsulationVerticalPosition: float = -1.5035  # -1503.5 [mm]	# WARNING: multiple values: ['-1503.5 [mm]', '1503.5 [mm]']
	# Radial position of the inner surface of the bottom stack reflector
	BottomStackInsulationRadialPosition: float = 0.664  # 664 [mm]
	# Diameter of the grooves of the bottom stack reflector
	BottomStackInsulationGrooveDiameter: float = 0.001  # 1 [mm]	# WARNING: multiple values: ['1 [mm]', '4 [mm]']
	# Vertical position of the top most groove of the bottom stack reflector with respect to its upper surface
	BottomStackInsulationGrooveTopVerticalPosition: float = 0.00178  # 1.78 [mm]
	# Vertical pitch between two grooves in the botto stack reflector+
	BottomStackInsualtionGrooveVerticalPitch: float = 0.002  # 2 [mm]
	# Vertical pitch between two grooves in the botto stack reflector+
	BottomStackInsulationGrooveVerticalPitch: float = 0.002  # 2 [mm]
	# Height of the field shaping guard
	GuardHeight: float = 0.015  # 15 [mm]
	# Width of the field shaping guard
	GuardWidth: float = 0.005  # 5 [mm]	# WARNING: multiple values: ['10 [mm]', '5 [mm]']
	# Vertical position of the center of the top most field shaping guard
	GuardTopVerticalPosition: float = -0.078125  # -78.125 [mm]	# WARNING: multiple values: ['-63.125 [mm]', '-78.125 [mm]']
	# Radial position of the inner face of the field shaping guard
	GuardRadialPosition: float = 0.677687  # 677.687 [mm]
	# Vertical pitch of the field shaping guard
	GuardVerticalPitch: float = 0.022  # 22 [mm]
	# Number of field shaping guards
	GuardNumber: float = 0.0  # original: '64'
	# Radial position of the bottom holder of the bottom screen electrode frame
	BottomScreenInsulationHolderRadialPosition: float = 0.69025  # 690.25 [mm]
	# Radial position of the bottom holder of the bottom screen electrode frame
	BottomScreenHolderRadialPosition: float = 0.69025  # 690.25 [mm]
	# Vertical position of the bottom holder of the bottom screen electrode frame
	BottomScreenHolderVerticalPosition: float = -1.558  # -1558 [mm]	# WARNING: multiple values: ['-1558 [mm]', '690.25 [mm]']
	# Width of the bottom holder of the bottom screen electrode frame
	BottomScreenHolderWidth: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['-1558 [mm]', '2 [mm]']
	# Height of the bottom holder of the bottom screen electrode frame
	BottomScreenHolderHeight: float = 0.005  # 5 [mm]	# WARNING: multiple values: ['2 [mm]', '5 [mm]']
	# Chamfer of the bottom holder of the bottom screen electrode frame
	BottomScreenHolderChamfer: float = 0.001  # 1 [mm]	# WARNING: multiple values: ['1 [mm]', '5 [mm]']
	# Position of the closest wire to the guard frame
	GateFirstWireRadialPosition: float = 0.0  # original: 'TopStackRadialPosition-4.5[mm]'	# WARNING: multiple values: ['TopStackRadialPosition-4.497[mm]', 'TopStackRadialPosition-4.5[mm]']
	# Position of the closest wire to the anode frame
	AnodeFirstWireRadialPosition: float = 0.0  # original: 'TopStackRadialPosition-7[mm]'	# WARNING: multiple values: ['TopStackRadialPosition-7[mm]', 'TopStackRadialPosition-8.101[mm]']
	# Position of the closest wire to the top screen frame
	TopScreenFirstWireRadialPosition: float = 0.0  # original: 'TopStackRadialPosition-4.5[mm]'
	# Position of the closest wire to the cathode frame
	CathodeFirstWireRadialPosition: float = 0.6675  # 667.5 [mm]	# WARNING: multiple values: ['667.5 [mm]', 'TopStackRadialPosition-7[mm]']
	# Position of the closest wire to the frame for the bottom stack electrodes (cathode and bottom screen)
	BottomStackFirstWireRadialPosition: float = 0.6675  # 667.5 [mm]
	# Height of the bottom PMTs array reflector
	BottomPMTReflectorHeight: float = 0.008  # 8 [mm]
	# Height of the bottom PMTs array reflector
	BottomPMTsReflectorHeight: float = 0.008  # 8 [mm]
	# Diameter of the bottom PMTs array reflector
	BottomPMTsArrayReflectorDiameter: float = 1.395  # 1395 [mm]
	# Height of the bottom PMTs array reflector
	BottomPMTsArrayReflectorHeight: float = 0.008  # 8 [mm]
	# Vertical position of the top face of the bottom PMTs array reflector
	BottomPMTsArrayReflectorVerticalPosition: float = -1.56  # -1560 [mm]	# WARNING: multiple values: ['-1560 [mm]', '1560 [mm]']
	# Width of the notch in the bottom PMTs array reflector
	BottomPMTsArrayReflectorNotchWidth: float = 0.002  # 2 [mm]
	# Radial position of the notch in the bottom PMTs array reflector
	BottomPMTsArrayReflectorNotchRadialPosition: float = 0.69025  # 690.25 [mm]	# WARNING: multiple values: ['2 [mm]', '690.25 [mm]']
	# Depth of the notch in the bottom PMTs array reflector
	BottomPMTsArrayReflectorNotchDepth: float = 0.003  # 3 [mm]	# WARNING: multiple values: ['2 [mm]', '3 [mm]']
	# Diameter of the holes for the PMT in the bottom PMT array
	BottomPMTsArrayReflectorPMTHoleDiameter: float = 0.069  # 69 [mm]
	# Chamfer of the holes for the PMT in the bottom PMT array
	BottomPMTsArrayReflectorPMTHoleChamfer: float = 0.002  # 2 [mm]	# WARNING: multiple values: ['2 [mm]', '69 [mm]']
	# Depth of the holes for the PMT in the bottom PMT array
	BottomPMTsArrayReflectorPMTHoleDepth: float = 0.0029  # 2.9 [mm]	# WARNING: multiple values: ['2 [mm]', '2.9 [mm]']
	# Position of the outest PMT in the hexagonal bottom PMTs array
	BottomPMTsArrayReflectorPMTFirstRadialPosition: float = 0.648  # 648 [mm]	# WARNING: multiple values: ['2.9 [mm]', '648 [mm]']
	# Position of the outest PMT in the hexagonal bottom PMTs array
	BottomPMTsArrayPMTFirstRadialPosition: float = 0.648  # 648 [mm]
	# Diameter of the holes for the PMT in the bottom PMT array
	BottomPMTsArrayReflectorPMTHoleHousingRadius: float = 0.0068  # 6.8 [mm]	# WARNING: multiple values: ['6.8 [mm]', '69 [mm]']
	# Diameter of the hole housing the PMT in the bottom PMT array
	BottomPMTsArrayReflectorPMTHoleHousingDiameter: float = 0.0785  # 78.5 [mm]	# WARNING: multiple values: ['6.8 [mm]', '78.5 [mm]']
	# Radial pitch of the holes for the PMT in the bottom PMT array
	BottomPMTsArrayReflectorPMTHoleRadialPitch: float = 0.081  # 81 [mm]	# WARNING: multiple values: ['69 [mm]', '81 [mm]']
	# Major diameter of the PMT
	PMTMajorDiameter: float = 0.076  # 76 [mm]
	# Quarzdiameter of the PMT
	PMTQuarzDiameter: float = 0.076  # 76 [mm]
	# Quartz diameter of the PMT
	PMTQuartzDiameter: float = 0.076  # 76 [mm]
	# Diameter of the body of the PMT
	PMTBodyDiameter: float = 0.0533  # 53.3 [mm]
	# Diameter of the basis of the PMT
	PMTBasisDiameter: float = 0.0318  # 31.8 [mm]
	# Height of the quartz of the PMT
	PMTQuartzHeight: float = 0.0135  # 13.5 [mm]
	# Height of the rounding of the PMT body
	PMTRoundingHeight: float = 0.0271  # 27.1 [mm]
	# Height of the body of the PMT (including rounding quartz)
	PMTBodyHeight: float = 0.1005  # 100.5 [mm]	# WARNING: multiple values: ['100.5[mm]', '114[mm]', '73.4 [mm]']
	# Height of the PMT basis
	PMTBasisHeight: float = 0.00955  # 9.55 [mm]
	# Position of the outest PMT in the hexagonal top PMTs array
	TopPMTsArrayPMTFirstRadialPosition: float = 0.648  # 648 [mm]
	# Diameter of the top PMTs array reflector
	TopPMTsArrayReflectorDiameter: float = 1.412  # 1412 [mm]
	# Height of the top PMTs array reflector
	TopPMTsArrayReflectorHeight: float = 0.008  # 8 [mm]
	# Depth of the notch in the bottom PMTs array reflector
	TPMTsArrayReflectorNotchDepth: float = 0.003  # 3 [mm]
	# Chamfer of the holes for the PMT in the top PMT array
	TopPMTsArrayReflectorPMTHoleChamfer: float = 0.002  # 2 [mm]
	# Depth of the holes for the PMT in the top PMT array
	TopPMTsArrayReflectorPMTHoleDepth: float = 0.0029  # 2.9 [mm]
	# Diameter of the holes for the PMT in the top PMT array
	TopPMTsArrayReflectorPMTHoleDiameter: float = 0.069  # 69 [mm]
	# Diameter of the hole housing the PMT in the top PMT array
	TopPMTsArrayReflectorPMTHoleHousingDiameter: float = 0.0785  # 78.5 [mm]
	# Radial pitch of the holes for the PMT in the top PMT array
	TopPMTsArrayReflectorPMTHoleRadialPitch: float = 0.081  # 81 [mm]
	# Vertical position of the top face of the top PMTs array reflector
	TopPMTsArrayReflectorVerticalPosition: float = 0.0739  # 73.9 [mm]	# WARNING: multiple values: ['69.9 [mm]', '69.96 [mm]', '73.9 [mm]']
	# Outer diameter of the diving bell
	BellOuterDiameter: float = 0.713  # 713 [mm]
	# Height of the diving bell
	BellHeight: float = 0.264  # 264 [mm]
	# Minor thickness of the diving bell
	BellMinorThickness: float = 0.004  # 4 [mm]
	# Major thickness of the diving bell
	BellMajorThickness: float = 0.005  # 5 [mm]
	# Vertical position of the increase in thickness of the diving bell
	BellThicknessIncreaseVerticalPosition: float = 0.159  # 159 [mm]
	# Vertical position of the top face of the bell
	BellVerticalPosition: float = 0.239  # 239 [mm]
	# Vertical position of the bottom of the straight section of the inner cryostat
	CryostatStraightVerticalPosition: float = -1.60824  # -1608.24 [mm]
	# Height of the straight section of the inner cryostat
	CryostatStraightHeight: float = 1.8671  # 1867.1 [mm]
	# Liquid level above the gate
	LiquidLevel: float = 0.004  # 4 [mm]
	# LowerCryostataUpperRadius (no description found)
	LowerCryostataUpperRadius: float = 0.22  # 220 [mm]
	# Radius of the upper part of the bottom of the cryostat
	LowerCryostatUpperRadius: float = 0.22  # 220 [mm]
	# Radius of the lower part of the bottom of the cryostat
	CryostatBottomLowerRadius: float = 1.2  # 1200 [mm]	# WARNING: multiple values: ['1200 [mm]', '220 [mm]']
	# Radius of the upper part of the bottom of the cryostat
	CryostatBottomUpperRadius: float = 0.22  # 220 [mm]
	# Vertical position of the center of the lower roudning of the bottom of the inner cryostat
	CryostatBottomLowerCenterVerticalPosition: float = -0.760142  # -760.142 [mm]
	# Radius of the lower part of the top of the cryostat
	CryostatTopLowerRadius: float = 0.22  # 220 [mm]	# WARNING: multiple values: ['1200 [mm]', '220 [mm]']
	# Radius of the upper part of the top of the cryostat
	CryostatTopUpperRadius: float = 1.2  # 1200 [mm]	# WARNING: multiple values: ['1200 [mm]', '220 [mm]']
	# Diameter of the pipe opening on the top of the inner cryostat
	CryostatTopPipeDiameter: float = 0.378  # 378 [mm]
	# dV (no description found)
	dV: float = 0.0
	# VoltageDropFieldCage (no description found)
	VoltageDropFieldCage: float = 0.0
	# Vertical position of the top wire of the field cage in warm condition
	WireTopFieldCageVerticalPosition: float = 0.0  # original: 'WireTopVerticalPosition-2*WireVerticalPitch'
	# TopExtraction (no description found)
	TopExtraction: float = 0.0
	# TopExport (no description found)
	TopExport: float = 0.0
	# BottomExtraction (no description found)
	BottomExtraction: float = 0.0
	# BottomExport (no description found)
	BottomExport: float = 0.0
	# RadialExtraction (no description found)
	RadialExtraction: float = 0.0
	# RadialExport (no description found)
	RadialExport: float = 0.0
	# Change of the radial width and position of the copper ring - TEMPORARY
	CopperRingModificationTemporary: float = 0.002  # 2 [mm]
	# Dimension L of the anode insulating frame (see drawing AnodeInsulatingFrame)
	AnodeInsulationDimensionN: float = 0.0148  # 14.8 [mm]	# WARNING: multiple values: ['13 [mm]', '14.8 [mm]', '19.5 [mm]']
	# PMTCasing (no description found)
	PMTCasing: float = 0.0
	# bPMTCasing (no description found)
	bPMTCasing: float = 0.0
	# ResistanceForFirstAndLastSetofDividers (no description found)
	ResistanceForFirstAndLastSetofDividers: float = 0.0
	# RFirst5FieldShaping (no description found)
	RFirst5FieldShaping: float = 0.0
	# ResistanceLastFieldShapingAndCathode (no description found)
	ResistanceLastFieldShapingAndCathode: float = 0.0
	# RInParallelFieldShaping (no description found)
	RInParallelFieldShaping: float = 0.0
	# ResistanceCircuitSplitter (no description found)
	ResistanceCircuitSplitter: float = 0.0
	# RLastTwoFieldShaping (no description found)
	RLastTwoFieldShaping: float = 0.0
	# RFieldShapingToCathode (no description found)
	RFieldShapingToCathode: float = 0.0
	# CopperRing (no description found)
	CopperRing: float = 0.0
	# VCopperRing (no description found)
	VCopperRing: float = 0.0
	# TopFieldShapingRing (no description found)
	TopFieldShapingRing: float = 0.0
	# VTopFieldShapingRing (no description found)
	VTopFieldShapingRing: float = 0.0
	# GateElectrode (no description found)
	GateElectrode: float = 0.0
	# VGateElectrode (no description found)
	VGateElectrode: float = 0.0
	# AnodeElectrode (no description found)
	AnodeElectrode: float = 0.0
	# VAnodeElectrode (no description found)
	VAnodeElectrode: float = 0.0
	# TopShieldingElectrode (no description found)
	TopShieldingElectrode: float = 0.0
	# VTopShieldingElectrode (no description found)
	VTopShieldingElectrode: float = 0.0
	# VPMTCasing (no description found)
	VPMTCasing: float = 0.0
	# Bell (no description found)
	Bell: float = 0.0
	# VBell (no description found)
	VBell: float = 0.0
	# ParallelSegmentGuard (no description found)
	ParallelSegmentGuard: float = 0.0
	# RParallelSegmentGuard (no description found)
	RParallelSegmentGuard: float = 0.0
	# ParallelSegmentRings (no description found)
	ParallelSegmentRings: float = 0.0
	# RParallelSegmentRings (no description found)
	RParallelSegmentRings: float = 0.0
	# CathodeToGate (no description found)
	CathodeToGate: float = 0.0
	# CathodeToGateDistance (no description found)
	CathodeToGateDistance: float = 0.0
	# V2ndFieldShaping (no description found)
	V2ndFieldShaping: float = 0.0
	# VDropFirst5FieldShaping (no description found)
	VDropFirst5FieldShaping: float = 0.0



def build_lower_cryostat(part_doc, p):
    """
    2D cross-section of the lower cryostat as ONE closed contour:
      - 3 lines: axis, top, right wall
      - lower rounding arc (arc11)
      - upper rounding arc (arc10) connecting arc11 to the right wall
      - closing line from axis down to arc11
    """
    Sketch_1 = model.addSketch(part_doc, model.defaultPlane("XOY"))
    Sketch_1.setName("LowerCryostatSketch")

    R = p.CryostatDiameter / 2.0

    # --------------------------------------------------------
    # Levels
    # --------------------------------------------------------
    y1 = p.CryostatStraightVerticalPosition
    y2 = p.CryostatStraightVerticalPosition + abs(p.CryostatStraightVerticalPosition) + p.LiquidLevel

    # --------------------------------------------------------
    # Straight section: 3 lines (no rectangle primitive)
    #   - axis:   (0, y1) -> (0, y2)
    #   - top:    (0, y2) -> (R, y2)
    #   - right:  (R, y2) -> (R, y1)
    # --------------------------------------------------------
    line_axis_rect = Sketch_1.addLine(0.0, y1, 0.0, y2)
    line_axis_rect.setName("AxisRect")
    line_top = Sketch_1.addLine(0.0, y2, R,   y2)
    line_top.setName("TopLine")
    line_right = Sketch_1.addLine(R,   y2, R, y1)
    line_right.setName("RightWall")

    # --------------------------------------------------------
    # LOWER rounding: arc11 (as in your original code)
    # --------------------------------------------------------
    R_upper = p.CryostatBottomUpperRadius
    R_lower = p.CryostatBottomLowerRadius

    term_radius_diff = R_lower - R_upper
    dx = R - R_upper
    alpha = math.asin(dx / term_radius_diff)      # sector angle [rad]

    term1 = (R_lower - R_upper) ** 2
    term2 = (R - R_upper) ** 2
    dy = math.sqrt(term1 - term2)

    cx11 = 0.0
    cy11 = p.CryostatStraightVerticalPosition + dy
    r11  = R_lower

    theta_start = -math.pi / 2.0
    theta_end   = theta_start + alpha

    xs = cx11 + r11 * math.cos(theta_start)
    ys = cy11 + r11 * math.sin(theta_start)
    xe = cx11 + r11 * math.cos(theta_end)
    ye = cy11 + r11 * math.sin(theta_end)

    arc11 = Sketch_1.addArc(cx11, cy11, xs, ys, xe, ye, False)
    arc11.setName("LowerCryostatArc")

    # --------------------------------------------------------
    # Closing line on axis: from straight section down to arc11
    #   (0, y1) -> (xs, ys)
    # --------------------------------------------------------
    line_axis = Sketch_1.addLine(0.0, y1, xs, ys)
    line_axis.setName("AxisClosingLine")

    # --------------------------------------------------------
    # UPPER rounding: replace full circle with an arc
    #  - same radius R_upper
    #  - connect arc11 end to right wall at (R, y1)
    # --------------------------------------------------------
    cx10 = R - R_upper
    cy10 = y1
    r10  = R_upper

    # Right-wall / rounding junction
    xR = R
    yR = y1

    # Upper rounding arc: from arc11 end (xe, ye) to right-wall foot (R, y1)
    # Note: we rely on geometry choice so that (xe, ye) lies on this circle.
    arc10 = Sketch_1.addArc(cx10, cy10, xe, ye, xR, yR, False)
    arc10.setName("UpperRoundingArc")

    # --------------------------------------------------------
    # Coincidence constraints to ensure ONE closed wire
    #  1) axis-rectangle to top line
    #  2) top line to right wall
    #  3) right wall to upper arc
    #  4) upper arc to lower arc
    #  5) lower arc to axis closing line
    #  6) axis closing line to axis-rectangle
    # --------------------------------------------------------
    # 1) top-left corner: AxisRect top = TopLine start
    Sketch_1.setCoincident(line_axis_rect.endPoint(), line_top.startPoint())

    # 2) top-right corner: TopLine end = RightWall start
    Sketch_1.setCoincident(line_top.endPoint(), line_right.startPoint())

    # 3) bottom-right corner: RightWall end = UpperRoundingArc end (R, y1)
    Sketch_1.setCoincident(line_right.endPoint(), arc10.endPoint())

    # 4) junction between upper and lower rounding: arc10 start = arc11 end
    Sketch_1.setCoincident(arc10.startPoint(), arc11.endPoint())

    # 5) junction between lower arc and axis-closing line: arc11 start = AxisClosingLine end
    Sketch_1.setCoincident(arc11.startPoint(), line_axis.endPoint())

    # 6) junction between axis-closing line and axis-rectangle: AxisClosingLine start = AxisRect start
    Sketch_1.setCoincident(line_axis.startPoint(), line_axis_rect.startPoint())

    model.do()  # finalize the sketch so its wires/faces exist

    #boundary_edges = [
    #      model.selection("EDGE", "LowerCryostatSketch/SketchLine_1"),
    #      model.selection("EDGE", "LowerCryostatSketch/SketchLine_2"),
    #      model.selection("EDGE", "LowerCryostatSketch/SketchLine_3"),
    #      model.selection("EDGE", "LowerCryostatSketch/SketchLine_4"),
    #      model.selection("EDGE", "LowerCryostatSketch/SketchArc_1_2"),
    #      model.selection("EDGE", "LowerCryostatSketch/SketchArc_2_2"),
    #  ]

    Cryostat = model.addFace(part_doc, [Sketch_1.result()])
    Cryostat.setName("LXeCryostat")

    return Cryostat

from salome.shaper import model
import math

def build_upper_cryostat(part_doc, p):
    """
    Upper cryostat + straight section as ONE closed contour, following the
    same construction logic as the COMSOL c12/c13 definitions.
    """

    Sketch_1 = model.addSketch(part_doc, model.defaultPlane("XOY"))
    Sketch_1.setName("UpperCryostatSketch")

    R = p.CryostatDiameter / 2.0

    # ------------------------------------------------------------------
    # Straight cylindrical section
    # ------------------------------------------------------------------
    y1 = p.LiquidLevel
    y2 = p.CryostatStraightHeight + p.CryostatStraightVerticalPosition

    # axis from liquid level y1 to cap base y2
    line_axis_rect = Sketch_1.addLine(0.0, y1, 0.0, y2)

    # bottom flat AT LIQUID LEVEL
    line_top = Sketch_1.addLine(0.0, y1, R, y1)

    # right wall from liquid level up to cap base
    line_right = Sketch_1.addLine(R, y1, R, y2)
    # ------------------------------------------------------------------
    # Parameters from COMSOL-style construction
    #   - R_lower_top:  radius of circle on the outer side (c12)
    #   - R_upper_top:  radius of circle on the axis (c13)
    # ------------------------------------------------------------------
    R_lower_top = p.CryostatTopLowerRadius   # COMSOL c12 radius
    R_upper_top = p.CryostatTopUpperRadius   # COMSOL c13 radius

    dx     = R - R_lower_top
    deltaR = R_upper_top - R_lower_top

    # COMSOL angle:
    alpha = math.asin(dx / deltaR)
    # vertical offset between c13 center and y2:
    dy    = math.sqrt(deltaR**2 - dx**2)

    # ------------------------------------------------------------------
    # Circle c13 (on axis): center (0, y2 - dy), radius = R_upper_top
    # This corresponds to the COMSOL final 'pos' for c13.
    # We only use a sector of this circle as an arc.
    # ------------------------------------------------------------------
    cx13 = 0.0
    cy13 = y2 - dy
    r13  = R_upper_top

    # COMSOL:
    #   angle = alpha
    #   rot   = 90° - alpha
    # → in radians:
    theta_s_small = math.pi/2.0 - alpha   # "rot"
    theta_e_small = math.pi/2.0           # rot + angle

    # Endpoints of the small arc on c13
    x_small_start = cx13 + r13 * math.cos(theta_s_small)  # common point with c12
    y_small_start = cy13 + r13 * math.sin(theta_s_small)
    x_small_end   = cx13 + r13 * math.cos(theta_e_small)  # axis-side point
    y_small_end   = cy13 + r13 * math.sin(theta_e_small)

    arc_small = Sketch_1.addArc(
        cx13, cy13,
        x_small_start, y_small_start,  # start: common point
        x_small_end,   y_small_end,    # end: axis-side point
        False
    )
    # If this still turns the wrong way, flip the last argument to True.

    # ------------------------------------------------------------------
    # Circle c12 (outer side): center (R - R_lower_top, y2),
    # radius = R_lower_top. We use only the arc between:
    #   - right-wall top corner (R, y2)
    #   - SAME common point as for c13 (x_small_start, y_small_start)
    # ------------------------------------------------------------------
    cx12 = R - R_lower_top
    cy12 = y2
    r12  = R_lower_top

    xR = R
    yR = y2

    arc_big = Sketch_1.addArc(
        cx12, cy12,
        xR, yR,                     # start at right-wall corner
        x_small_start, y_small_start,  # end at common point with small arc
        False
    )
    # Again, if orientation is inverted, toggle the last boolean.

    # ------------------------------------------------------------------
    # Axis-cap line: connect axis top (0, y2) to axis-side point of small arc
    # ------------------------------------------------------------------
        # axis-cap line from axis top y2 to small-arc axis-side point
    line_axis_cap = Sketch_1.addLine(0.0, y2, x_small_end, y_small_end)

    # ------------------------------------------------------------------
    # Coincidence constraints for one closed loop:
    # ------------------------------------------------------------------
    # 1) axis top ↔ axis_cap start
    Sketch_1.setCoincident(line_axis_rect.endPoint(),  line_axis_cap.startPoint())

    # 2) axis_cap end ↔ small arc end (axis side)
    Sketch_1.setCoincident(line_axis_cap.endPoint(),   arc_small.endPoint())

    # 3) small arc start ↔ big arc end
    Sketch_1.setCoincident(arc_small.startPoint(),     arc_big.endPoint())

    # 4) big arc start ↔ right wall top (R, y2)
    Sketch_1.setCoincident(arc_big.startPoint(),       line_right.endPoint())

    # 5) right wall bottom (R, y1) ↔ bottom flat right end
    Sketch_1.setCoincident(line_right.startPoint(),    line_top.endPoint())

    # 6) bottom flat left end (0, y1) ↔ axis bottom
    Sketch_1.setCoincident(line_top.startPoint(),      line_axis_rect.startPoint())

    model.do()

    # ------------------------------------------------------------------
    # Build the upper cryostat face from the six boundary edges.
    # Adjust the internal names via "Dump Python" if they differ.
    # ------------------------------------------------------------------
    #boundary_edges = [
    #    model.selection("EDGE", i) for i in 
    #    ("UpperCryostatSketch/SketchLine_8", "UpperCryostatSketch/SketchLine_5", "UpperCryostatSketch/SketchLine_6", "UpperCryostatSketch/SketchLine_7", "UpperCryostatSketch/SketchArc_4_2", "UpperCryostatSketch/SketchArc_3_2")
    #]
    boundary_edges = [Sketch_1.result()]

    Cryostat = model.addFace(part_doc, boundary_edges)
    Cryostat.setName("GXeCryostat")

    return Cryostat

def make_connected_boundary_from_ordered_points(Sketch, pts):
    # Create lines along the sequence
    lines = []
    for (x1, y1), (x2, y2) in zip(pts[:-1], pts[1:]):
        l = Sketch.addLine(x1, y1, x2, y2)
        lines.append(l)

    # Coincidence constraints to enforce a single closed wire
    for i in range(len(lines) - 1):
        Sketch.setCoincident(lines[i].endPoint(), lines[i+1].startPoint())
    # close last to first
    Sketch.setCoincident(lines[-1].endPoint(), lines[0].startPoint())
    return lines
def make_connected_boundary_from_ordered_points(Sketch, pts):
    """
    pts: list of N unique (x, y) points in order around the boundary.
    Returns N SketchLine objects forming a closed polygon.
    """
    n = len(pts)
    lines = []

    # Create N segments: from pts[i] to pts[(i+1) % n]
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        l = Sketch.addLine(x1, y1, x2, y2)
        lines.append(l)

    # Coincidence at each vertex
    for i in range(n):
        Sketch.setCoincident(lines[i].endPoint(),
                             lines[(i + 1) % n].startPoint())

    return lines

def build_gate(part_doc, p):
    Sketch = model.addSketch(part_doc, model.defaultPlane("XOY"))
    Sketch.setName("Gate")
    # The Gate ring 
    pts = [
        [p.TopStackRadialPosition, p.GateVerticalPosition-p.GateHeight+p.GateCutOutHeight],
        [p.TopStackRadialPosition, p.GateVerticalPosition],
        [p.TopStackRadialPosition+p.GateWidth, p.GateVerticalPosition],
        [p.TopStackRadialPosition+p.GateWidth, p.GateVerticalPosition-p.GateHeight],
        [p.TopStackRadialPosition+p.GateCutOutWidth, p.GateVerticalPosition-p.GateHeight],
        [p.TopStackRadialPosition+p.GateCutOutWidth, p.GateVerticalPosition-p.GateHeight+p.GateCutOutHeight],
    ]

    lines = make_connected_boundary_from_ordered_points(Sketch, pts)

    # For filleting we need to anchor the geometry 
    x_left_mid  = pts[0][0]
    y_left_mid  = 0.5*(pts[0][1] + pts[1][1])
    anchor_left = Sketch.addPoint(x_left_mid, y_left_mid)
    Sketch.setCoincident(anchor_left, lines[0].result())  # point lies on left vertical
    Sketch.setFixed(anchor_left)                          # lock position

    x_top_mid   = 0.5*(pts[1][0] + pts[2][0])
    y_top_mid   = pts[1][1]
    anchor_top  = Sketch.addPoint(x_top_mid, y_top_mid)
    Sketch.setCoincident(anchor_top, lines[1].result())   # point lies on top horizontal
    Sketch.setFixed(anchor_top)   

    # Fillet all external corners: indices 0,1,2,3,4
    fillet_indices = [0, 1, 2, 3, 4]
    # Build point mapping: corner i is the shared vertex = end of line[i-1]
    n = len(lines)
    for i in fillet_indices:
        vertex = lines[(i - 1) % n].endPoint()
        Sketch.setFilletWithRadius(vertex, p.GateDeburringRadius)


    # Gate wires 
    for i in range(floor(p.TopStackRadialPosition/p.TopStackWireSpacing)):
      # First center coordinates then a point the circle passes
      Sketch.addCircle(p.GateFirstWireRadialPosition+p.TopStackWireSpacing/4+i*p.TopStackWireSpacing, 
                       p.GateVerticalPosition,
                       p.GateFirstWireRadialPosition+p.TopStackWireSpacing/4+i*p.TopStackWireSpacing, 
                       p.GateVerticalPosition+50e-6)

    model.do()
    Gate = model.addFace(part_doc, [Sketch.result()])
    Gate.setName("Gate")

    return Gate

def build_anode(part_doc, p):
    Sketch = model.addSketch(part_doc, model.defaultPlane("XOY"))
    Sketch.setName("Anode")
    x0 = p.TopStackRadialPosition
    y0 = p.AnodeVerticalPosition
    W  = p.AnodeWidth
    H  = p.AnodeHeight

    pts = [
        (x0,     y0),       # bottom-left
        (x0,     y0 + H),   # top-left
        (x0 + W, y0 + H),   # top-right
        (x0 + W, y0),       # bottom-right
    ]

    lines = make_connected_boundary_from_ordered_points(Sketch, pts)

    # For filleting we need to anchor the geometry 
    x_left_mid  = pts[0][0]
    y_left_mid  = 0.5*(pts[0][1] + pts[1][1])
    anchor_left = Sketch.addPoint(x_left_mid, y_left_mid)
    Sketch.setCoincident(anchor_left, lines[0].result())  # point lies on left vertical
    Sketch.setFixed(anchor_left)                          # lock position

    x_top_mid   = 0.5*(pts[1][0] + pts[2][0])
    y_top_mid   = pts[1][1]
    anchor_top  = Sketch.addPoint(x_top_mid, y_top_mid)
    Sketch.setCoincident(anchor_top, lines[1].result())   # point lies on top horizontal
    Sketch.setFixed(anchor_top)   
    Sketch.setFixed( lines[1].result())   

    # Fillet all external corners: indices 0,1,2,3,4
    fillet_indices = [0,1,2,3]
    # Build point mapping: corner i is the shared vertex = end of line[i-1]
    n = len(lines)
    for i in fillet_indices:
        vertex = lines[(i - 1) % n].endPoint()
        Sketch.setFilletWithRadius(vertex, p.AnodeDeburringRadius)


    # Gate wires 
    for i in range(floor(p.TopStackRadialPosition/p.TopStackWireSpacing)):
      # First center coordinates then a point the circle passes
      Sketch.addCircle(p.AnodeFirstWireRadialPosition+p.TopStackWireSpacing/4+i*p.TopStackWireSpacing, 
                       p.AnodeVerticalPosition,
                       p.AnodeFirstWireRadialPosition+p.TopStackWireSpacing/4+i*p.TopStackWireSpacing, 
                       p.AnodeVerticalPosition+p.TopStackWireDiameter/2)

    model.do()
    Anode = model.addFace(part_doc, [Sketch.result()])
    Anode.setName("Anode")

    return Anode




def build_gate_insulating_frame(part_doc, p):
    pts = [
        [p.TopStackInsulationRadialPosition, p.GateInsulationVerticalPosition],
        [p.TopStackInsulationRadialPosition, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG+p.GateInsulationDimensionF],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE+p.GateInsulationDimensionD, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG+p.GateInsulationDimensionF],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE+p.GateInsulationDimensionD, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG+p.GateInsulationDimensionF-p.GateInsulationDimensionB],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE+p.GateInsulationDimensionD+p.GateInsulationDimensionC, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG+p.GateInsulationDimensionF-p.GateInsulationDimensionB],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE+p.GateInsulationDimensionD+p.GateInsulationDimensionC, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG+p.GateInsulationDimensionF-p.GateInsulationDimensionB-p.GateInsulationDimensionA],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE+p.GateInsulationDimensionD+p.GateInsulationDimensionC-p.GateInsulationDimensionK, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG+p.GateInsulationDimensionF-p.GateInsulationDimensionB-p.GateInsulationDimensionA],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE+p.GateInsulationDimensionD+p.GateInsulationDimensionC-p.GateInsulationDimensionK, p.GateInsulationVerticalPosition+p.GateInsulationDimensionG+p.GateInsulationDimensionF-p.GateInsulationDimensionB-p.GateInsulationDimensionA+p.GateInsulationDimensionJ],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE, p.GateInsulationVerticalPosition-p.GateInsulationDimensionH],
        [p.TopStackInsulationRadialPosition+p.GateInsulationDimensionE, p.GateInsulationVerticalPosition],
    ]
    Sketch = model.addSketch(part_doc, model.defaultPlane("XOY"))
    Sketch.setName("GateInsulatingFrame")
    lines = make_connected_boundary_from_ordered_points(Sketch, pts)
    model.do()
    GateInsulatingFrame = model.addFace(part_doc, [Sketch.result()])
    GateInsulatingFrame.setName("GateInsulatingFrame")

    return GateInsulatingFrame


if __name__ == '__main__':
  model.begin()

  partSet = model.moduleDocument()

  # Make the cryostat
  Cryostat = model.addPart(partSet)
  Cryostat_doc = Cryostat.document()
  p = GeometryParams()

  LXeCryostat = build_lower_cryostat(Cryostat_doc, p)
  GXeCryostat = build_upper_cryostat(Cryostat_doc, p)
  Gate = build_gate(Cryostat_doc, p)
  GateInsulatingFrame = build_gate_insulating_frame(Cryostat_doc, p)
  Anode = build_anode(Cryostat_doc, p)

  model.end()
