import { useFormContext } from "react-hook-form";

import type { WorldOption } from "../../lib/contracts";
import type { SearchFormValues } from "./searchSchema";

interface WorldSelectorProps {
  worlds: WorldOption[];
}

export default function WorldSelector({ worlds }: WorldSelectorProps) {
  const {
    register,
    formState: { errors },
  } = useFormContext<SearchFormValues>();

  return (
    <label className="field">
      <span>世界类型</span>
      <select {...register("worldType", { valueAsNumber: true })}>
        {worlds.map((world) => (
          <option key={world.id} value={world.id}>
            {world.id} - {world.code}
          </option>
        ))}
      </select>
      {errors.worldType ? <small className="error">{errors.worldType.message}</small> : null}
    </label>
  );
}
