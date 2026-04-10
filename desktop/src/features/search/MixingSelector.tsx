import { useFormContext } from "react-hook-form";

import type { SearchFormValues } from "./searchSchema";

export default function MixingSelector() {
  const {
    watch,
    register,
    formState: { errors },
  } = useFormContext<SearchFormValues>();
  const cpuMode = watch("cpuMode");

  return (
    <>
      <div className="field-grid">
        <label className="field">
          <span>Mixing</span>
          <input type="number" {...register("mixing", { valueAsNumber: true })} />
          {errors.mixing ? <small className="error">{errors.mixing.message}</small> : null}
        </label>
        <label className="field">
          <span>CPU 模式</span>
          <select {...register("cpuMode")}>
            <option value="balanced">平衡</option>
            <option value="turbo">极速</option>
            <option value="custom">自定义</option>
          </select>
          {errors.cpuMode ? <small className="error">{errors.cpuMode.message}</small> : null}
        </label>
      </div>

      <div className="field-grid">
        <label className="field">
          <span>线程数</span>
          <input
            type="number"
            placeholder={cpuMode === "custom" ? "自定义模式下必填" : "非自定义模式忽略该值"}
            {...register("threads", { valueAsNumber: true })}
          />
          {errors.threads ? <small className="error">{errors.threads.message}</small> : null}
        </label>
        <label className="field">
          <span>调度选项</span>
          <div className="field-inline">
            <label>
              <input type="checkbox" {...register("cpuAllowSmt")} />
              允许 SMT
            </label>
            <label>
              <input type="checkbox" {...register("cpuAllowLowPerf")} />
              允许低性能核心
            </label>
          </div>
        </label>
      </div>
    </>
  );
}
