"use client"

import React, { useState, useEffect, useCallback, useMemo, memo } from "react"
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs"
import { Button } from "@/components/ui/button"
import { RefreshCw, Play, StepForward, ChevronUp, ChevronDown, Hash, SquareCode } from "lucide-react"
import type { Simulator as SimulatorType } from "@/types/simulator"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle
} from "@/components/ui/dialog"
import type { SimulationControls } from "@/components/Landing";

const ITEMS_PER_PAGE = 10

enum Stage {
  FETCH = 0,
  DECODE = 1,
  EXECUTE = 2,
  MEMORY = 3,
  WRITEBACK = 4
}

class InstructionRegisters {
  RA: number;
  RB: number;
  RM: number;
  RY: number;
  RZ: number;

  constructor(RA = 0, RB = 0, RM = 0, RY = 0, RZ = 0) {
    this.RA = RA;
    this.RB = RB;
    this.RM = RM;
    this.RY = RY;
    this.RZ = RZ;
  }
}

interface SimulatorProps {
  text: string;
  simulatorInstance: SimulatorType;
  controls: SimulationControls;
  onSidebarOpenChange: (open: boolean) => void;
  onRunningChange: (running: boolean) => void;
}

const StageNames = {
  [Stage.FETCH]: 'FETCH',
  [Stage.DECODE]: 'DECODE',
  [Stage.EXECUTE]: 'EXECUTE',
  [Stage.MEMORY]: 'MEMORY',
  [Stage.WRITEBACK]: 'WRITEBACK'
};

const StageColors: Record<string, string> = {
  "FETCH": "#60A5FA",
  "DECODE": "#34D399",
  "EXECUTE": "#F59E0B",
  "MEMORY": "#EC4899",
  "WRITEBACK": "#8B5CF6",
};

type MemoryCell = {
  address: string
  value: string
  bytes: string[]
}

const toSignedDecimal = (value:number, bits = 32) => {
  const maxPositive = Math.pow(2, bits - 1) - 1;
  if (value > maxPositive) {
    return value - Math.pow(2, bits);
  }
  return value;
};

const byteToSignedDecimal = (value:number) => toSignedDecimal(value, 8);
const wordToSignedDecimal = (value:number) => toSignedDecimal(value, 32);

const MEMORY_SEGMENTS = {
  CODE: "0x00000000",
  DATA: "0x10000000",
  HEAP: "0x10008000",
  STACK: "0x7FFFFFFC",
  END: "0x80000000",
}

const REGISTER_ABI_NAMES = [
  "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0/fp", "s1",
  "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
  "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
  "t3", "t4", "t5", "t6",
]

const MemoryTable = memo(({ entries }: { entries: MemoryCell[] }) => (
  <table className="w-full text-sm">
    <thead>
      <tr className="bg-gray-50 border-b">
        <th className="py-2 px-4 text-left font-medium text-gray-500">Address</th>
        <th className="py-2 px-4 text-left font-medium text-gray-500">+3</th>
        <th className="py-2 px-4 text-left font-medium text-gray-500">+2</th>
        <th className="py-2 px-4 text-left font-medium text-gray-500">+1</th>
        <th className="py-2 px-4 text-left font-medium text-gray-500">+0</th>
      </tr>
    </thead>
    <tbody>
      {entries.map((entry, index) => (
        <tr key={index} className="border-b hover:bg-gray-50">
          <td className="py-2 px-4 font-mono">{entry.address === '----------' ? entry.address : `0x${entry.address}`}</td>
          <td className="py-2 px-4 font-mono">{entry.bytes[0]}</td>
          <td className="py-2 px-4 font-mono">{entry.bytes[1]}</td>
          <td className="py-2 px-4 font-mono">{entry.bytes[2]}</td>
          <td className="py-2 px-4 font-mono">{entry.bytes[3]}</td>
        </tr>
      ))}
    </tbody>
  </table>
));
MemoryTable.displayName = "MemoryTable";

const RegisterTable = memo(({
  registers,
  registerStartIndex,
  isHex
}: {
  registers: number[],
  registerStartIndex: number,
  isHex: boolean
}) => (
  <table className="w-full text-sm table-fixed">
    <thead>
      <tr className="bg-gray-50 border-b">
        <th className="py-2 px-4 text-left font-medium text-gray-500">Register</th>
        <th className="py-2 px-4 text-left font-medium text-gray-500">Value</th>
      </tr>
    </thead>
    <tbody>
      {registers.slice(registerStartIndex, registerStartIndex + ITEMS_PER_PAGE).map((reg, index) => {
        let hexValue;
        if (reg < 0) {
          hexValue = (reg >>> 0).toString(16).padStart(8, '0');
        } else {
          hexValue = reg.toString(16).padStart(8, '0');
        }
        const decimalValue = wordToSignedDecimal(reg >>> 0);

        return (
          <tr key={registerStartIndex + index} className="border-b hover:bg-gray-50">
            <td className="py-2 px-3 font-mono">
              {`x${registerStartIndex + index}`} <span className="text-gray-500">({REGISTER_ABI_NAMES[registerStartIndex + index]})</span>
            </td>
            <td className="py-2 px-3 font-mono">
              {isHex ? `0x${hexValue}` : decimalValue}
            </td>
          </tr>
        );
      })}
    </tbody>
  </table>
));
RegisterTable.displayName = "RegisterTable";

const PipelineStages = memo(({ activeStates, running }: { 
  activeStates: Record<number, { active: boolean, instruction: number }>, 
  running: boolean 
}) => {
  const stageEntries = Object.entries(StageNames).map(([stageNumStr, stageName]) => {
    const stageNum = parseInt(stageNumStr);
    const stageState = activeStates[stageNum] || { active: false, instruction: 0 };
    
    return (
      <div
        key={stageName}
        className={`p-2 border rounded ${(stageState.active && running) ? "opacity-100" : "opacity-30"}`}
        style={{ backgroundColor: StageColors[stageName] }}
      >
        <div className="font-semibold text-white">{stageName}</div>
        <div className="text-xs text-white/90">
          {stageState.active ? "Active" : "Idle"}
        </div>
      </div>
    );
  });

  return (
    <div className="grid grid-cols-5 gap-2 mb-4">
      {stageEntries}
    </div>
  );
});
PipelineStages.displayName = "PipelineStages";

interface ErrorBoundaryProps {
  children: React.ReactNode;
}

interface ErrorBoundaryState {
  hasError: boolean;
  error: Error | null;
}

class SimulatorErrorBoundary extends React.Component<ErrorBoundaryProps, ErrorBoundaryState> {
  constructor(props: ErrorBoundaryProps) {
    super(props);
    this.state = { hasError: false, error: null };
  }

  static getDerivedStateFromError(error: Error) {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, errorInfo: React.ErrorInfo) {
    console.error('Simulator error:', error, errorInfo);
  }

  render() {
    if (this.state.hasError) {
      return (
        <div className="flex flex-col items-center justify-center h-full p-4">
          <h2 className="text-xl font-semibold text-red-600 mb-2">Something went wrong</h2>
          <p className="text-gray-600 mb-4">{this.state.error?.message}</p>
          <Button
            variant="outline"
            onClick={() => this.setState({ hasError: false, error: null })}
          >
            Try again
          </Button>
        </div>
      );
    }

    return this.props.children;
  }
}

interface TextMapEntry {
  first: number;
  second: string;
}

export default function Simulator({ 
  text, 
  simulatorInstance, 
  controls, 
  onSidebarOpenChange,
  onRunningChange 
}: SimulatorProps) {
  const [registers, setRegisters] = useState<number[]>(Array.from({ length: 32 }, () => 0));
  const [cycles, setCycles] = useState<number>(0);
  const [dataMap, setDataMap] = useState<Record<string, number>>({});
  const [textMap, setTextMap] = useState<Record<string, { first: number, second: string }>>({});
  const [terminal, setTerminal] = useState<Record<string, string>>({});
  const [running, setRunning] = useState<boolean>(false);
  const [activeStates, setActiveStates] = useState<Record<number, { active: boolean, instruction: number }>>({
    [Stage.FETCH]: { active: false, instruction: 0 },
    [Stage.DECODE]: { active: false, instruction: 0 },
    [Stage.EXECUTE]: { active: false, instruction: 0 },
    [Stage.MEMORY]: { active: false, instruction: 0 },
    [Stage.WRITEBACK]: { active: false, instruction: 0 }
  });
  const [pipelineRegisters, setPipelineRegisters] = useState<InstructionRegisters>(new InstructionRegisters());
  const [isHex, setIsHex] = useState<boolean>(true);
  const [instruction, setInstruction] = useState<number>(0);
  const [stalls, setStalls] = useState<number>(0);

  const [memoryStartIndex, setMemoryStartIndex] = useState<number>(0);
  const [memoryEntries, setMemoryEntries] = useState<MemoryCell[]>([]);
  const [displayFormat, setDisplayFormat] = useState<"hex" | "decimal">("hex");
  const [registerStartIndex, setRegisterStartIndex] = useState(0);
  const [isDialogOpen, setIsDialogOpen] = useState(false);
  const [dialogMessage, setDialogMessage] = useState({
    title: "",
    description: ""
  });

  const showTerminalErrorDialog = useCallback((errorMessage: string) => {
    setDialogMessage({
      title: "Error 404: Not Found",
      description: errorMessage
    });
    setIsDialogOpen(true);
    if (simulatorInstance) {
      simulatorInstance.reset();
    }
  }, [simulatorInstance]);
  
  const updateSimulatorState = useCallback(() => {
    if (!simulatorInstance) return;

    const newTerminal = simulatorInstance.getLogs();
    setTerminal(newTerminal);
    setRegisters(simulatorInstance.getRegisters());
    setCycles(simulatorInstance.getCycles());
    setDataMap(simulatorInstance.getDataMap());
    setTextMap(simulatorInstance.getTextMap());
    setStalls(simulatorInstance.getStalls());
    setActiveStates(simulatorInstance.getActiveStages());
    setRunning(simulatorInstance.isRunning());
    setPipelineRegisters(simulatorInstance.getInstructionRegisters());
    setInstruction(simulatorInstance.getPC());
    if (newTerminal["404"]) {
      setTimeout(() => showTerminalErrorDialog(newTerminal["404"]), 0);
    }
  }, [simulatorInstance, showTerminalErrorDialog]);

  const memoizedUpdateMemoryEntries = useMemo(() => {
    return () => {
      const entries = Array.from({ length: ITEMS_PER_PAGE }, (_, index) => {
        const addressNum = memoryStartIndex + index * 4;
        const address = addressNum.toString();
        const addressString = addressNum.toString(16).padStart(8, '0').toUpperCase();
  
        if (addressNum >= parseInt(MEMORY_SEGMENTS.END, 16)) {
          return {
            address: "----------",
            value: "00000000",
            bytes: ["--", "--", "--", "--"]
          };
        }
  
        let value = 0;
        let found = false;
        let bytesDisplay = ["00", "00", "00", "00"];
  
        if (addressNum >= 0x00000000 && addressNum <= 0x0FFFFFFC && textMap[address]) {
          value = textMap[address].first;
          found = true;
        }
        else if (addressNum >= 0x10000000 && addressNum <= 0x80000000) {
          const byteAddresses = [
            addressNum,
            addressNum + 1,
            addressNum + 2,
            addressNum + 3
          ];
          const bytes = byteAddresses.map(addr => {
            const addrStr = addr.toString();
            return dataMap[addrStr] !== undefined ? dataMap[addrStr] : 0;
          });
          if (bytes.some(b => b !== 0)) {
            found = true;
            value = (bytes[3] << 0) | (bytes[2] << 8) | (bytes[1] << 16) | (bytes[0] << 24);
            bytesDisplay = [
              bytes[3], bytes[2], bytes[1], bytes[0]
            ].map(byte => {
              let byteValue = byte;
              if (byteValue < 0) {
                byteValue = byteValue & 0xFF;
              }
              const byteHex = byteValue.toString(16).padStart(2, '0').toUpperCase();
              const byteDecimal = isHex ? byteHex : byteToSignedDecimal(byteValue);
              return isHex ? byteHex : byteDecimal.toString();
            });
          }
        }
        let valueHex;
        if (value < 0) {
          valueHex = (value >>> 0).toString(16).padStart(8, '0').toUpperCase();
        } else {
          valueHex = value.toString(16).padStart(8, '0').toUpperCase();
        }
        return {
          address: addressString,
          value: valueHex,
          bytes: found ? bytesDisplay : ["00", "00", "00", "00"]
        };
      });
      setMemoryEntries(entries);
    };
  }, [memoryStartIndex, isHex, textMap, dataMap]);

  const handleStep = useCallback(() => {
    simulatorInstance.step();
    updateSimulatorState();
  }, [simulatorInstance, updateSimulatorState]);

  const resetRegistersAndMemory = useCallback(() => {
    simulatorInstance.reset();
    updateSimulatorState();
    memoizedUpdateMemoryEntries();
  }, [simulatorInstance, updateSimulatorState, memoizedUpdateMemoryEntries]);

  const handleRun = useCallback(() => {
    simulatorInstance.run();
    updateSimulatorState();
  }, [simulatorInstance, updateSimulatorState]);

  useEffect(() => {
    console.log(activeStates)
  },[activeStates])

  const handleAssemble = useCallback(() => {
    simulatorInstance.loadProgram(text);
    updateSimulatorState();
  }, [text, simulatorInstance, updateSimulatorState]);

  useEffect(() => {
    updateSimulatorState();
  }, [updateSimulatorState]);

  useEffect(() => {
    memoizedUpdateMemoryEntries();
  }, [memoizedUpdateMemoryEntries]);

  useEffect(() => {
    if (running) {
      onSidebarOpenChange(false);
    }
    onRunningChange(running);
  }, [running, onSidebarOpenChange, onRunningChange]);

  useEffect(() => {
    if (!simulatorInstance) return;
    simulatorInstance.setEnvironment(
      controls.pipelining,
      controls.dataForwarding
    );
    updateSimulatorState();
  }, [controls.pipelining, controls.dataForwarding, simulatorInstance, updateSimulatorState]);

  const navigateMemoryUp = () => {
    setMemoryStartIndex(prev => Math.max(0, prev - ITEMS_PER_PAGE * 4));
  };

  const navigateMemoryDown = () => {
    setMemoryStartIndex(prev => {
      const maxAddr = parseInt(MEMORY_SEGMENTS.END, 16) - (ITEMS_PER_PAGE * 4);
      return Math.min(maxAddr, prev + ITEMS_PER_PAGE * 4);
    });
  };

  const toggleDisplayFormat = () => {
    setDisplayFormat(displayFormat === "hex" ? "decimal" : "hex");
    setIsHex(!isHex);
  };

  const jumpToSegment = (segment: string) => {
    let baseAddress = 0;
    switch (segment) {
      case MEMORY_SEGMENTS.CODE: baseAddress = 0x00000000; break;
      case MEMORY_SEGMENTS.DATA: baseAddress = 0x10000000; break;
      case MEMORY_SEGMENTS.HEAP: baseAddress = 0x10008000; break;
      case MEMORY_SEGMENTS.STACK: baseAddress = 0x7ffffffc; break;
    }
    setMemoryStartIndex(baseAddress);
  };

  const navigateRegistersUp = () => {
    if (registerStartIndex > 0) {
      setRegisterStartIndex(prev => Math.max(0, prev - ITEMS_PER_PAGE));
    }
  };

  const navigateRegistersDown = () => {
    if (registerStartIndex + ITEMS_PER_PAGE < registers.length) {
      setRegisterStartIndex(prev => Math.min(registers.length - ITEMS_PER_PAGE, prev + ITEMS_PER_PAGE));
    }
  };

  const renderTerminalOutput = () => {
    if (Object.entries(terminal).length === 0) {
      return running ? (
        <div className="text-gray-500">
          Nothing to log.
        </div>
      ) : (
        <div className="text-gray-500">
          No code loaded. Assemble RISC-V machine code to begin.
        </div>
      );
    }

    return Object.entries(terminal).map(([key, value], index) => {
      switch (key) {
        case '400':
          return (
            <div key={index} className="mb-1 text-red-600">
              {value}
            </div>
          );
        case '300':
          return (
            <div key={index} className="mb-1 text-orange-600">
              {value}
            </div>
          );
        case '200':
          return (
            <div key={index} className="mb-1 text-green-600">
              {value}
            </div>
          );
        default:
          return (
            <div key={index} className="mb-1">
              {value}
            </div>
          );
      }
    });
  };

  return (
    <SimulatorErrorBoundary>
      <div className="w-full h-full p-[2%] relative">
        <Dialog open={isDialogOpen} onOpenChange={setIsDialogOpen}>
          <DialogContent>
            <DialogHeader>
              <DialogTitle>{dialogMessage.title}</DialogTitle>
              <DialogDescription>{dialogMessage.description}</DialogDescription>
            </DialogHeader>
            <Button onClick={() => setIsDialogOpen(false)}>Close</Button>
          </DialogContent>
        </Dialog>
        <div className="grid grid-rows-8 h-full gap-[2%]">
          <div className="row-span-6 grid grid-cols-1 xl:grid-cols-2 gap-[2%]">
            <div className="border rounded-lg overflow-hidden bg-white shadow-sm h-full flex flex-col">
              <div className="bg-gray-100 py-2 px-3 flex justify-between items-center border-b min-h-[8%]">
                <div className="flex gap-2 flex-wrap">
                  <Button variant="outline" size="sm" onClick={handleAssemble}>
                    <SquareCode className="h-4 w-4 mr-2" />
                    Assemble
                  </Button>
                  <Button variant="outline" size="sm" onClick={resetRegistersAndMemory}>
                    <RefreshCw className="h-4 w-4 mr-2" />
                    Reset
                  </Button>
                </div>
              </div>

              <div className="flex-1 overflow-hidden">
                <div className="h-full flex flex-col">
                  <div className="h-[90%] overflow-y-auto scrollbar-thin scrollbar-thumb-gray-300 scrollbar-track-gray-100">
                    <table className="w-full text-sm table-fixed">
                      <thead className="sticky top-0 bg-white shadow-sm z-10">
                        <tr className="bg-gray-50 border-b">
                          <th className="py-2 px-3 text-left font-medium text-gray-500 w-[25%]">PC</th>
                          <th className="py-2 px-3 text-left font-medium text-gray-500 w-[25%]">Machine Code</th>
                          <th className="py-2 px-3 text-left font-medium text-gray-500 w-[50%]">Basic Code</th>
                        </tr>
                      </thead>
                      <tbody className="divide-y divide-gray-100">
                        {Object.keys(textMap).length > 0 ? (
                          Object.entries(textMap as Record<string, TextMapEntry>).map(([key, instObj]) => {
                            const instructionAddress = parseInt(key);
                            let highlightColor = "";
                            Object.entries(activeStates).forEach(([stageKey, stageInfo]) => {
                              if (stageInfo.active && stageInfo.instruction === instructionAddress) {
                                const stageNum = parseInt(stageKey) as Stage;
                                const stageName = StageNames[stageNum];
                                highlightColor = StageColors[stageName];
                              }
                            });
                            
                            return (
                              <tr key={key} className="hover:bg-gray-50 h-10">
                                <td 
                                  className="py-2 px-3 font-mono whitespace-nowrap"
                                  style={{ backgroundColor: highlightColor, color: highlightColor ? "white" : "inherit" }}
                                >
                                  0x{instructionAddress.toString(16).padStart(8, '0').toUpperCase()}
                                </td>
                                <td 
                                  className="py-2 px-3 font-mono whitespace-nowrap"
                                  style={{ backgroundColor: highlightColor, color: highlightColor ? "white" : "inherit" }}
                                >
                                  0x{instObj.first.toString(16).padStart(8, '0').toUpperCase()}
                                </td>
                                <td 
                                  className="py-2 px-3 font-mono whitespace-nowrap"
                                  style={{ backgroundColor: highlightColor, color: highlightColor ? "white" : "inherit" }}
                                >
                                  {instObj.second}
                                </td>
                              </tr>
                            );
                          })
                        ) : (
                          <tr>
                            <td colSpan={3} className="py-8 text-center text-gray-500">
                              No code loaded. Upload a RISC-V machine code file to view assembled code.
                            </td>
                          </tr>
                        )}
                      </tbody>
                    </table>
                  </div>

                  <div className="bg-gray-100 py-2 px-3 flex justify-between gap-2 border-t mt-auto min-h-[8%]">
                    <div className="flex items-center gap-2 text-sm text-gray-600">
                      <span>Total Instructions: {Object.keys(textMap).length ?? 0}</span>
                    </div>
                    <div className="flex gap-2">
                      <Button
                        variant="outline"
                        size="sm"
                        onClick={handleStep}
                        disabled={!running}
                      >
                        <StepForward className="h-4 w-4 mr-2" />
                        Step
                      </Button>
                      <Button
                        variant="default"
                        size="sm"
                        onClick={handleRun}
                        disabled={!running}
                      >
                        <Play className="h-4 w-4 mr-2" />
                        Run
                      </Button>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <div className="border rounded-lg overflow-hidden bg-white shadow-sm h-full flex flex-col">
              <div className="bg-gray-100 py-2 px-3 border-b min-h-[8%]">
                <h2 className="text-lg font-semibold">System Board</h2>
              </div>

              <div className="flex-1 flex flex-col overflow-hidden">
                <Tabs defaultValue="registers" className="flex-1 flex flex-col">
                  <TabsList className="grid w-full grid-cols-2 min-h-[8%] h-full">
                    <TabsTrigger value="registers">Registers</TabsTrigger>
                    <TabsTrigger value="memory">Memory</TabsTrigger>
                  </TabsList>

                  <TabsContent value="registers" className="flex-1 flex flex-col data-[state=inactive]:hidden">
                    <div className="bg-gray-100 py-2 px-3 flex justify-between items-center border-b min-h-[8%]">
                      <div className="flex items-center gap-4">
                        <div className="text-sm text-gray-500">Register Values</div>
                      </div>
                      <div className="flex gap-2">
                        <div className="flex gap-1">
                          <Button
                            variant="outline"
                            size="sm"
                            onClick={navigateRegistersUp}
                            disabled={registerStartIndex <= 0}
                          >
                            <ChevronUp className="h-4 w-4" />
                          </Button>
                          <Button
                            variant="outline"
                            size="sm"
                            onClick={navigateRegistersDown}
                            disabled={registerStartIndex + ITEMS_PER_PAGE >= registers.length}
                          >
                            <ChevronDown className="h-4 w-4" />
                          </Button>
                        </div>
                        <Button variant="outline" size="sm" onClick={toggleDisplayFormat}>
                          <Hash className="h-4 w-4 mr-2" />
                          {displayFormat === "hex" ? "Show Decimal" : "Show Hex"}
                        </Button>
                      </div>
                    </div>
                    <div className="overflow-y-auto flex-1">
                      <RegisterTable
                        registers={registers}
                        registerStartIndex={registerStartIndex}
                        isHex={isHex}
                      />
                    </div>
                  </TabsContent>

                  <TabsContent value="memory" className="flex-1 flex flex-col data-[state=inactive]:hidden">
                    <div className="bg-gray-100 py-2 px-3 flex flex-col sm:flex-row justify-between items-center gap-2 border-b min-h-[8%]">
                      <div className="flex flex-wrap gap-2">
                        <Button variant="outline" size="sm" onClick={() => jumpToSegment(MEMORY_SEGMENTS.CODE)}>
                          Code
                        </Button>
                        <Button variant="outline" size="sm" onClick={() => jumpToSegment(MEMORY_SEGMENTS.DATA)}>
                          Data
                        </Button>
                        <Button variant="outline" size="sm" onClick={() => jumpToSegment(MEMORY_SEGMENTS.HEAP)}>
                          Heap
                        </Button>
                        <Button variant="outline" size="sm" onClick={() => jumpToSegment(MEMORY_SEGMENTS.STACK)}>
                          Stack
                        </Button>
                      </div>
                      <div className="flex items-center gap-2">
                        <div className="flex gap-1">
                          <Button
                            variant="outline"
                            size="sm"
                            onClick={navigateMemoryUp}
                            disabled={memoryStartIndex <= 0}
                          >
                            <ChevronUp className="h-4 w-4" />
                          </Button>
                          <Button
                            variant="outline"
                            size="sm"
                            onClick={navigateMemoryDown}
                            disabled={memoryStartIndex >= parseInt(MEMORY_SEGMENTS.END, 16) - (ITEMS_PER_PAGE * 4)}
                          >
                            <ChevronDown className="h-4 w-4" />
                          </Button>
                        </div>
                        <Button variant="outline" size="sm" onClick={toggleDisplayFormat}>
                          <Hash className="h-4 w-4 mr-2" />
                          {displayFormat === "hex" ? "Decimal" : "Hex"}
                        </Button>
                      </div>
                    </div>
                    <div className="overflow-y-auto flex-1">
                      <MemoryTable entries={memoryEntries} />
                    </div>
                  </TabsContent>
                </Tabs>

                <div className="bg-gray-50 py-2 px-3 border-t mt-auto min-h-[15%]">
                  <PipelineStages activeStates={activeStates} running={running} />
                </div>
              </div>
            </div>
          </div>

          <div className="row-span-2 grid grid-cols-1 md:grid-cols-2 gap-[2%]">
            <div className="border rounded-lg overflow-hidden bg-white shadow-sm h-full flex flex-col">
              <div className="bg-gray-100 py-2 px-3 border-b min-h-[15%]">
                <h2 className="text-lg font-semibold">Console Output</h2>
              </div>
              <div className="flex-1 p-3 font-mono text-sm bg-gray-50 overflow-y-auto">
                {renderTerminalOutput()}
              </div>
            </div>
            <div className="border rounded-lg overflow-hidden bg-white shadow-sm h-full flex flex-col">
              <div className="bg-gray-100 py-2 px-3 border-b min-h-[15%]">
                <h2 className="text-lg font-semibold">System Information</h2>
              </div>
              <div className="flex-1 p-3 font-mono text-sm bg-gray-50 overflow-y-auto">
                <div className="grid grid-cols-2 gap-4">
                  <div>
                    <h3 className="font-semibold mb-2">System Variables</h3>
                    <div className="text-xs space-y-1">
                      <div>RA: {`0x${((pipelineRegisters["RA"] ?? 0) >>> 0).toString(16).toUpperCase().padStart(8, '0')}`}</div>
                      <div>RB: {`0x${((pipelineRegisters["RB"] ?? 0) >>> 0).toString(16).toUpperCase().padStart(8, '0')}`}</div>
                      <div>RM: {`0x${((pipelineRegisters["RM"] ?? 0) >>> 0).toString(16).toUpperCase().padStart(8, '0')}`}</div>
                      <div>RY: {`0x${((pipelineRegisters["RY"] ?? 0) >>> 0).toString(16).toUpperCase().padStart(8, '0')}`}</div>
                    </div>
                  </div>
                  <div>
                    <h3 className="font-semibold mb-2">&nbsp;</h3>
                    <div className="text-xs space-y-1">
                      <div>Stalls: {!running ? 0 : stalls}</div>
                      <div>Clock Cycles: {cycles ?? 0}</div>
                      <div>PC: 0x{(instruction && running) ?
                        ((instruction) >>> 0).toString(16).toUpperCase().padStart(8, '0') :
                        '00000000'}</div>
                      <div>RZ: {`0x${((pipelineRegisters["RZ"] ?? 0) >>> 0).toString(16).toUpperCase().padStart(8, '0')}`}</div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </SimulatorErrorBoundary>
  )
}