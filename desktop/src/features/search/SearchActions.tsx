interface SearchActionsProps {
  isSearching: boolean;
  isCancelling: boolean;
  onCancel: () => void;
  onClear: () => void;
  onCopy: () => void;
}

export default function SearchActions({
  isSearching,
  isCancelling,
  onCancel,
  onClear,
  onCopy,
}: SearchActionsProps) {
  return (
    <section className="search-actions">
      <button type="submit" className="primary" disabled={isSearching}>
        {isSearching ? "搜索进行中..." : "开始搜索"}
      </button>
      <button type="button" onClick={onCancel} disabled={!isSearching || isCancelling}>
        {isCancelling ? "取消中..." : "取消搜索"}
      </button>
      <button type="button" onClick={onClear}>
        清空结果
      </button>
      <button type="button" onClick={onCopy}>
        复制协议 JSON
      </button>
    </section>
  );
}
